//===-- IRInterpreter.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/IRInterpreter.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanCallFunctionUsingABI.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/raw_ostream.h"

#include <map>

using namespace llvm;

static std::string PrintValue(const Value *value, bool truncate = false) {
  std::string s;
  raw_string_ostream rso(s);
  value->print(rso);
  rso.flush();
  if (truncate)
    s.resize(s.length() - 1);

  size_t offset;
  while ((offset = s.find('\n')) != s.npos)
    s.erase(offset, 1);
  while (s[0] == ' ' || s[0] == '\t')
    s.erase(0, 1);

  return s;
}

static std::string PrintType(const Type *type, bool truncate = false) {
  std::string s;
  raw_string_ostream rso(s);
  type->print(rso);
  rso.flush();
  if (truncate)
    s.resize(s.length() - 1);
  return s;
}

static bool CanIgnoreCall(const CallInst *call) {
  const llvm::Function *called_function = call->getCalledFunction();

  if (!called_function)
    return false;

  if (called_function->isIntrinsic()) {
    switch (called_function->getIntrinsicID()) {
    default:
      break;
    case llvm::Intrinsic::dbg_declare:
    case llvm::Intrinsic::dbg_value:
      return true;
    }
  }

  return false;
}

class InterpreterStackFrame {
public:
  typedef std::map<const Value *, lldb::addr_t> ValueMap;

  ValueMap m_values;
  DataLayout &m_target_data;
  lldb_private::IRExecutionUnit &m_execution_unit;
  const BasicBlock *m_bb;
  const BasicBlock *m_prev_bb;
  BasicBlock::const_iterator m_ii;
  BasicBlock::const_iterator m_ie;

  lldb::addr_t m_frame_process_address;
  size_t m_frame_size;
  lldb::addr_t m_stack_pointer;

  lldb::ByteOrder m_byte_order;
  size_t m_addr_byte_size;

  InterpreterStackFrame(DataLayout &target_data,
                        lldb_private::IRExecutionUnit &execution_unit,
                        lldb::addr_t stack_frame_bottom,
                        lldb::addr_t stack_frame_top)
      : m_target_data(target_data), m_execution_unit(execution_unit),
        m_bb(nullptr), m_prev_bb(nullptr) {
    m_byte_order = (target_data.isLittleEndian() ? lldb::eByteOrderLittle
                                                 : lldb::eByteOrderBig);
    m_addr_byte_size = (target_data.getPointerSize(0));

    m_frame_process_address = stack_frame_bottom;
    m_frame_size = stack_frame_top - stack_frame_bottom;
    m_stack_pointer = stack_frame_top;
  }

  ~InterpreterStackFrame() {}

  void Jump(const BasicBlock *bb) {
    m_prev_bb = m_bb;
    m_bb = bb;
    m_ii = m_bb->begin();
    m_ie = m_bb->end();
  }

  std::string SummarizeValue(const Value *value) {
    lldb_private::StreamString ss;

    ss.Printf("%s", PrintValue(value).c_str());

    ValueMap::iterator i = m_values.find(value);

    if (i != m_values.end()) {
      lldb::addr_t addr = i->second;

      ss.Printf(" 0x%llx", (unsigned long long)addr);
    }

    return ss.GetString();
  }

  bool AssignToMatchType(lldb_private::Scalar &scalar, uint64_t u64value,
                         Type *type) {
    size_t type_size = m_target_data.getTypeStoreSize(type);

    if (type_size > 8)
      return false;

    if (type_size != 1)
      type_size = PowerOf2Ceil(type_size);

    scalar = llvm::APInt(type_size*8, u64value);
    return true;
  }

  bool EvaluateValue(lldb_private::Scalar &scalar, const Value *value,
                     Module &module) {
    const Constant *constant = dyn_cast<Constant>(value);

    if (constant) {
      APInt value_apint;

      if (!ResolveConstantValue(value_apint, constant))
        return false;

      return AssignToMatchType(scalar, value_apint.getLimitedValue(),
                               value->getType());
    } else {
      lldb::addr_t process_address = ResolveValue(value, module);
      size_t value_size = m_target_data.getTypeStoreSize(value->getType());

      lldb_private::DataExtractor value_extractor;
      lldb_private::Status extract_error;

      m_execution_unit.GetMemoryData(value_extractor, process_address,
                                     value_size, extract_error);

      if (!extract_error.Success())
        return false;

      lldb::offset_t offset = 0;
      if (value_size <= 8) {
        uint64_t u64value = value_extractor.GetMaxU64(&offset, value_size);
        return AssignToMatchType(scalar, u64value, value->getType());
      }
    }

    return false;
  }

  bool AssignValue(const Value *value, lldb_private::Scalar &scalar,
                   Module &module) {
    lldb::addr_t process_address = ResolveValue(value, module);

    if (process_address == LLDB_INVALID_ADDRESS)
      return false;

    lldb_private::Scalar cast_scalar;

    if (!AssignToMatchType(cast_scalar, scalar.ULongLong(), value->getType()))
      return false;

    size_t value_byte_size = m_target_data.getTypeStoreSize(value->getType());

    lldb_private::DataBufferHeap buf(value_byte_size, 0);

    lldb_private::Status get_data_error;

    if (!cast_scalar.GetAsMemoryData(buf.GetBytes(), buf.GetByteSize(),
                                     m_byte_order, get_data_error))
      return false;

    lldb_private::Status write_error;

    m_execution_unit.WriteMemory(process_address, buf.GetBytes(),
                                 buf.GetByteSize(), write_error);

    return write_error.Success();
  }

  bool ResolveConstantValue(APInt &value, const Constant *constant) {
    switch (constant->getValueID()) {
    default:
      break;
    case Value::FunctionVal:
      if (const Function *constant_func = dyn_cast<Function>(constant)) {
        lldb_private::ConstString name(constant_func->getName());
        lldb::addr_t addr = m_execution_unit.FindSymbol(name);
        if (addr == LLDB_INVALID_ADDRESS)
          return false;
        value = APInt(m_target_data.getPointerSizeInBits(), addr);
        return true;
      }
      break;
    case Value::ConstantIntVal:
      if (const ConstantInt *constant_int = dyn_cast<ConstantInt>(constant)) {
        value = constant_int->getValue();
        return true;
      }
      break;
    case Value::ConstantFPVal:
      if (const ConstantFP *constant_fp = dyn_cast<ConstantFP>(constant)) {
        value = constant_fp->getValueAPF().bitcastToAPInt();
        return true;
      }
      break;
    case Value::ConstantExprVal:
      if (const ConstantExpr *constant_expr =
              dyn_cast<ConstantExpr>(constant)) {
        switch (constant_expr->getOpcode()) {
        default:
          return false;
        case Instruction::IntToPtr:
        case Instruction::PtrToInt:
        case Instruction::BitCast:
          return ResolveConstantValue(value, constant_expr->getOperand(0));
        case Instruction::GetElementPtr: {
          ConstantExpr::const_op_iterator op_cursor = constant_expr->op_begin();
          ConstantExpr::const_op_iterator op_end = constant_expr->op_end();

          Constant *base = dyn_cast<Constant>(*op_cursor);

          if (!base)
            return false;

          if (!ResolveConstantValue(value, base))
            return false;

          op_cursor++;

          if (op_cursor == op_end)
            return true; // no offset to apply!

          SmallVector<Value *, 8> indices(op_cursor, op_end);

          Type *src_elem_ty =
              cast<GEPOperator>(constant_expr)->getSourceElementType();
          uint64_t offset =
              m_target_data.getIndexedOffsetInType(src_elem_ty, indices);

          const bool is_signed = true;
          value += APInt(value.getBitWidth(), offset, is_signed);

          return true;
        }
        }
      }
      break;
    case Value::ConstantPointerNullVal:
      if (isa<ConstantPointerNull>(constant)) {
        value = APInt(m_target_data.getPointerSizeInBits(), 0);
        return true;
      }
      break;
    }
    return false;
  }

  bool MakeArgument(const Argument *value, uint64_t address) {
    lldb::addr_t data_address = Malloc(value->getType());

    if (data_address == LLDB_INVALID_ADDRESS)
      return false;

    lldb_private::Status write_error;

    m_execution_unit.WritePointerToMemory(data_address, address, write_error);

    if (!write_error.Success()) {
      lldb_private::Status free_error;
      m_execution_unit.Free(data_address, free_error);
      return false;
    }

    m_values[value] = data_address;

    lldb_private::Log *log(
        lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    if (log) {
      log->Printf("Made an allocation for argument %s",
                  PrintValue(value).c_str());
      log->Printf("  Data region    : %llx", (unsigned long long)address);
      log->Printf("  Ref region     : %llx", (unsigned long long)data_address);
    }

    return true;
  }

  bool ResolveConstant(lldb::addr_t process_address, const Constant *constant) {
    APInt resolved_value;

    if (!ResolveConstantValue(resolved_value, constant))
      return false;

    size_t constant_size = m_target_data.getTypeStoreSize(constant->getType());
    lldb_private::DataBufferHeap buf(constant_size, 0);

    lldb_private::Status get_data_error;

    lldb_private::Scalar resolved_scalar(
        resolved_value.zextOrTrunc(llvm::NextPowerOf2(constant_size) * 8));
    if (!resolved_scalar.GetAsMemoryData(buf.GetBytes(), buf.GetByteSize(),
                                         m_byte_order, get_data_error))
      return false;

    lldb_private::Status write_error;

    m_execution_unit.WriteMemory(process_address, buf.GetBytes(),
                                 buf.GetByteSize(), write_error);

    return write_error.Success();
  }

  lldb::addr_t Malloc(size_t size, uint8_t byte_alignment) {
    lldb::addr_t ret = m_stack_pointer;

    ret -= size;
    ret -= (ret % byte_alignment);

    if (ret < m_frame_process_address)
      return LLDB_INVALID_ADDRESS;

    m_stack_pointer = ret;
    return ret;
  }

  lldb::addr_t Malloc(llvm::Type *type) {
    lldb_private::Status alloc_error;

    return Malloc(m_target_data.getTypeAllocSize(type),
                  m_target_data.getPrefTypeAlignment(type));
  }

  std::string PrintData(lldb::addr_t addr, llvm::Type *type) {
    size_t length = m_target_data.getTypeStoreSize(type);

    lldb_private::DataBufferHeap buf(length, 0);

    lldb_private::Status read_error;

    m_execution_unit.ReadMemory(buf.GetBytes(), addr, length, read_error);

    if (!read_error.Success())
      return std::string("<couldn't read data>");

    lldb_private::StreamString ss;

    for (size_t i = 0; i < length; i++) {
      if ((!(i & 0xf)) && i)
        ss.Printf("%02hhx - ", buf.GetBytes()[i]);
      else
        ss.Printf("%02hhx ", buf.GetBytes()[i]);
    }

    return ss.GetString();
  }

  lldb::addr_t ResolveValue(const Value *value, Module &module) {
    ValueMap::iterator i = m_values.find(value);

    if (i != m_values.end())
      return i->second;

    // Fall back and allocate space [allocation type Alloca]

    lldb::addr_t data_address = Malloc(value->getType());

    if (const Constant *constant = dyn_cast<Constant>(value)) {
      if (!ResolveConstant(data_address, constant)) {
        lldb_private::Status free_error;
        m_execution_unit.Free(data_address, free_error);
        return LLDB_INVALID_ADDRESS;
      }
    }

    m_values[value] = data_address;
    return data_address;
  }
};

static const char *unsupported_opcode_error =
    "Interpreter doesn't handle one of the expression's opcodes";
static const char *unsupported_operand_error =
    "Interpreter doesn't handle one of the expression's operands";
// static const char *interpreter_initialization_error = "Interpreter couldn't
// be initialized";
static const char *interpreter_internal_error =
    "Interpreter encountered an internal error";
static const char *bad_value_error =
    "Interpreter couldn't resolve a value during execution";
static const char *memory_allocation_error =
    "Interpreter couldn't allocate memory";
static const char *memory_write_error = "Interpreter couldn't write to memory";
static const char *memory_read_error = "Interpreter couldn't read from memory";
static const char *infinite_loop_error = "Interpreter ran for too many cycles";
// static const char *bad_result_error                 = "Result of expression
// is in bad memory";
static const char *too_many_functions_error =
    "Interpreter doesn't handle modules with multiple function bodies.";

static bool CanResolveConstant(llvm::Constant *constant) {
  switch (constant->getValueID()) {
  default:
    return false;
  case Value::ConstantIntVal:
  case Value::ConstantFPVal:
  case Value::FunctionVal:
    return true;
  case Value::ConstantExprVal:
    if (const ConstantExpr *constant_expr = dyn_cast<ConstantExpr>(constant)) {
      switch (constant_expr->getOpcode()) {
      default:
        return false;
      case Instruction::IntToPtr:
      case Instruction::PtrToInt:
      case Instruction::BitCast:
        return CanResolveConstant(constant_expr->getOperand(0));
      case Instruction::GetElementPtr: {
        ConstantExpr::const_op_iterator op_cursor = constant_expr->op_begin();
        Constant *base = dyn_cast<Constant>(*op_cursor);
        if (!base)
          return false;

        return CanResolveConstant(base);
      }
      }
    } else {
      return false;
    }
  case Value::ConstantPointerNullVal:
    return true;
  }
}

bool IRInterpreter::CanInterpret(llvm::Module &module, llvm::Function &function,
                                 lldb_private::Status &error,
                                 const bool support_function_calls) {
  lldb_private::Log *log(
      lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  bool saw_function_with_body = false;

  for (Module::iterator fi = module.begin(), fe = module.end(); fi != fe;
       ++fi) {
    if (fi->begin() != fi->end()) {
      if (saw_function_with_body) {
        if (log)
          log->Printf("More than one function in the module has a body");
        error.SetErrorToGenericError();
        error.SetErrorString(too_many_functions_error);
        return false;
      }
      saw_function_with_body = true;
    }
  }

  for (Function::iterator bbi = function.begin(), bbe = function.end();
       bbi != bbe; ++bbi) {
    for (BasicBlock::iterator ii = bbi->begin(), ie = bbi->end(); ii != ie;
         ++ii) {
      switch (ii->getOpcode()) {
      default: {
        if (log)
          log->Printf("Unsupported instruction: %s", PrintValue(&*ii).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(unsupported_opcode_error);
        return false;
      }
      case Instruction::Add:
      case Instruction::Alloca:
      case Instruction::BitCast:
      case Instruction::Br:
      case Instruction::PHI:
        break;
      case Instruction::Call: {
        CallInst *call_inst = dyn_cast<CallInst>(ii);

        if (!call_inst) {
          error.SetErrorToGenericError();
          error.SetErrorString(interpreter_internal_error);
          return false;
        }

        if (!CanIgnoreCall(call_inst) && !support_function_calls) {
          if (log)
            log->Printf("Unsupported instruction: %s",
                        PrintValue(&*ii).c_str());
          error.SetErrorToGenericError();
          error.SetErrorString(unsupported_opcode_error);
          return false;
        }
      } break;
      case Instruction::GetElementPtr:
        break;
      case Instruction::ICmp: {
        ICmpInst *icmp_inst = dyn_cast<ICmpInst>(ii);

        if (!icmp_inst) {
          error.SetErrorToGenericError();
          error.SetErrorString(interpreter_internal_error);
          return false;
        }

        switch (icmp_inst->getPredicate()) {
        default: {
          if (log)
            log->Printf("Unsupported ICmp predicate: %s",
                        PrintValue(&*ii).c_str());

          error.SetErrorToGenericError();
          error.SetErrorString(unsupported_opcode_error);
          return false;
        }
        case CmpInst::ICMP_EQ:
        case CmpInst::ICMP_NE:
        case CmpInst::ICMP_UGT:
        case CmpInst::ICMP_UGE:
        case CmpInst::ICMP_ULT:
        case CmpInst::ICMP_ULE:
        case CmpInst::ICMP_SGT:
        case CmpInst::ICMP_SGE:
        case CmpInst::ICMP_SLT:
        case CmpInst::ICMP_SLE:
          break;
        }
      } break;
      case Instruction::And:
      case Instruction::AShr:
      case Instruction::IntToPtr:
      case Instruction::PtrToInt:
      case Instruction::Load:
      case Instruction::LShr:
      case Instruction::Mul:
      case Instruction::Or:
      case Instruction::Ret:
      case Instruction::SDiv:
      case Instruction::SExt:
      case Instruction::Shl:
      case Instruction::SRem:
      case Instruction::Store:
      case Instruction::Sub:
      case Instruction::Trunc:
      case Instruction::UDiv:
      case Instruction::URem:
      case Instruction::Xor:
      case Instruction::ZExt:
        break;
      }

      for (int oi = 0, oe = ii->getNumOperands(); oi != oe; ++oi) {
        Value *operand = ii->getOperand(oi);
        Type *operand_type = operand->getType();

        switch (operand_type->getTypeID()) {
        default:
          break;
        case Type::VectorTyID: {
          if (log)
            log->Printf("Unsupported operand type: %s",
                        PrintType(operand_type).c_str());
          error.SetErrorString(unsupported_operand_error);
          return false;
        }
        }

        // The IR interpreter currently doesn't know about
        // 128-bit integers. As they're not that frequent,
        // we can just fall back to the JIT rather than
        // choking.
        if (operand_type->getPrimitiveSizeInBits() > 64) {
          if (log)
            log->Printf("Unsupported operand type: %s",
                        PrintType(operand_type).c_str());
          error.SetErrorString(unsupported_operand_error);
          return false;
        }

        if (Constant *constant = llvm::dyn_cast<Constant>(operand)) {
          if (!CanResolveConstant(constant)) {
            if (log)
              log->Printf("Unsupported constant: %s",
                          PrintValue(constant).c_str());
            error.SetErrorString(unsupported_operand_error);
            return false;
          }
        }
      }
    }
  }

  return true;
}

bool IRInterpreter::Interpret(llvm::Module &module, llvm::Function &function,
                              llvm::ArrayRef<lldb::addr_t> args,
                              lldb_private::IRExecutionUnit &execution_unit,
                              lldb_private::Status &error,
                              lldb::addr_t stack_frame_bottom,
                              lldb::addr_t stack_frame_top,
                              lldb_private::ExecutionContext &exe_ctx) {
  lldb_private::Log *log(
      lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  if (log) {
    std::string s;
    raw_string_ostream oss(s);

    module.print(oss, NULL);

    oss.flush();

    log->Printf("Module as passed in to IRInterpreter::Interpret: \n\"%s\"",
                s.c_str());
  }

  DataLayout data_layout(&module);

  InterpreterStackFrame frame(data_layout, execution_unit, stack_frame_bottom,
                              stack_frame_top);

  if (frame.m_frame_process_address == LLDB_INVALID_ADDRESS) {
    error.SetErrorString("Couldn't allocate stack frame");
  }

  int arg_index = 0;

  for (llvm::Function::arg_iterator ai = function.arg_begin(),
                                    ae = function.arg_end();
       ai != ae; ++ai, ++arg_index) {
    if (args.size() <= static_cast<size_t>(arg_index)) {
      error.SetErrorString("Not enough arguments passed in to function");
      return false;
    }

    lldb::addr_t ptr = args[arg_index];

    frame.MakeArgument(&*ai, ptr);
  }

  uint32_t num_insts = 0;

  frame.Jump(&function.front());

  while (frame.m_ii != frame.m_ie && (++num_insts < 4096)) {
    const Instruction *inst = &*frame.m_ii;

    if (log)
      log->Printf("Interpreting %s", PrintValue(inst).c_str());

    switch (inst->getOpcode()) {
    default:
      break;

    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::SDiv:
    case Instruction::UDiv:
    case Instruction::SRem:
    case Instruction::URem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor: {
      const BinaryOperator *bin_op = dyn_cast<BinaryOperator>(inst);

      if (!bin_op) {
        if (log)
          log->Printf(
              "getOpcode() returns %s, but instruction is not a BinaryOperator",
              inst->getOpcodeName());
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *lhs = inst->getOperand(0);
      Value *rhs = inst->getOperand(1);

      lldb_private::Scalar L;
      lldb_private::Scalar R;

      if (!frame.EvaluateValue(L, lhs, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(lhs).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      if (!frame.EvaluateValue(R, rhs, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(rhs).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      lldb_private::Scalar result;

      switch (inst->getOpcode()) {
      default:
        break;
      case Instruction::Add:
        result = L + R;
        break;
      case Instruction::Mul:
        result = L * R;
        break;
      case Instruction::Sub:
        result = L - R;
        break;
      case Instruction::SDiv:
        L.MakeSigned();
        R.MakeSigned();
        result = L / R;
        break;
      case Instruction::UDiv:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = L / R;
        break;
      case Instruction::SRem:
        L.MakeSigned();
        R.MakeSigned();
        result = L % R;
        break;
      case Instruction::URem:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = L % R;
        break;
      case Instruction::Shl:
        result = L << R;
        break;
      case Instruction::AShr:
        result = L >> R;
        break;
      case Instruction::LShr:
        result = L;
        result.ShiftRightLogical(R);
        break;
      case Instruction::And:
        result = L & R;
        break;
      case Instruction::Or:
        result = L | R;
        break;
      case Instruction::Xor:
        result = L ^ R;
        break;
      }

      frame.AssignValue(inst, result, module);

      if (log) {
        log->Printf("Interpreted a %s", inst->getOpcodeName());
        log->Printf("  L : %s", frame.SummarizeValue(lhs).c_str());
        log->Printf("  R : %s", frame.SummarizeValue(rhs).c_str());
        log->Printf("  = : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::Alloca: {
      const AllocaInst *alloca_inst = dyn_cast<AllocaInst>(inst);

      if (!alloca_inst) {
        if (log)
          log->Printf("getOpcode() returns Alloca, but instruction is not an "
                      "AllocaInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      if (alloca_inst->isArrayAllocation()) {
        if (log)
          log->Printf(
              "AllocaInsts are not handled if isArrayAllocation() is true");
        error.SetErrorToGenericError();
        error.SetErrorString(unsupported_opcode_error);
        return false;
      }

      // The semantics of Alloca are:
      //   Create a region R of virtual memory of type T, backed by a data
      //   buffer
      //   Create a region P of virtual memory of type T*, backed by a data
      //   buffer
      //   Write the virtual address of R into P

      Type *T = alloca_inst->getAllocatedType();
      Type *Tptr = alloca_inst->getType();

      lldb::addr_t R = frame.Malloc(T);

      if (R == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("Couldn't allocate memory for an AllocaInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_allocation_error);
        return false;
      }

      lldb::addr_t P = frame.Malloc(Tptr);

      if (P == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("Couldn't allocate the result pointer for an AllocaInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_allocation_error);
        return false;
      }

      lldb_private::Status write_error;

      execution_unit.WritePointerToMemory(P, R, write_error);

      if (!write_error.Success()) {
        if (log)
          log->Printf("Couldn't write the result pointer for an AllocaInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_write_error);
        lldb_private::Status free_error;
        execution_unit.Free(P, free_error);
        execution_unit.Free(R, free_error);
        return false;
      }

      frame.m_values[alloca_inst] = P;

      if (log) {
        log->Printf("Interpreted an AllocaInst");
        log->Printf("  R : 0x%" PRIx64, R);
        log->Printf("  P : 0x%" PRIx64, P);
      }
    } break;
    case Instruction::BitCast:
    case Instruction::ZExt: {
      const CastInst *cast_inst = dyn_cast<CastInst>(inst);

      if (!cast_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns %s, but instruction is not a BitCastInst",
              cast_inst->getOpcodeName());
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *source = cast_inst->getOperand(0);

      lldb_private::Scalar S;

      if (!frame.EvaluateValue(S, source, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(source).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      frame.AssignValue(inst, S, module);
    } break;
    case Instruction::SExt: {
      const CastInst *cast_inst = dyn_cast<CastInst>(inst);

      if (!cast_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns %s, but instruction is not a BitCastInst",
              cast_inst->getOpcodeName());
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *source = cast_inst->getOperand(0);

      lldb_private::Scalar S;

      if (!frame.EvaluateValue(S, source, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(source).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      S.MakeSigned();

      lldb_private::Scalar S_signextend(S.SLongLong());

      frame.AssignValue(inst, S_signextend, module);
    } break;
    case Instruction::Br: {
      const BranchInst *br_inst = dyn_cast<BranchInst>(inst);

      if (!br_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns Br, but instruction is not a BranchInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      if (br_inst->isConditional()) {
        Value *condition = br_inst->getCondition();

        lldb_private::Scalar C;

        if (!frame.EvaluateValue(C, condition, module)) {
          if (log)
            log->Printf("Couldn't evaluate %s", PrintValue(condition).c_str());
          error.SetErrorToGenericError();
          error.SetErrorString(bad_value_error);
          return false;
        }

        if (!C.IsZero())
          frame.Jump(br_inst->getSuccessor(0));
        else
          frame.Jump(br_inst->getSuccessor(1));

        if (log) {
          log->Printf("Interpreted a BrInst with a condition");
          log->Printf("  cond : %s", frame.SummarizeValue(condition).c_str());
        }
      } else {
        frame.Jump(br_inst->getSuccessor(0));

        if (log) {
          log->Printf("Interpreted a BrInst with no condition");
        }
      }
    }
      continue;
    case Instruction::PHI: {
      const PHINode *phi_inst = dyn_cast<PHINode>(inst);

      if (!phi_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns PHI, but instruction is not a PHINode");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }
      if (!frame.m_prev_bb) {
        if (log)
          log->Printf("Encountered PHI node without having jumped from another "
                      "basic block");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *value = phi_inst->getIncomingValueForBlock(frame.m_prev_bb);
      lldb_private::Scalar result;
      if (!frame.EvaluateValue(result, value, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(value).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }
      frame.AssignValue(inst, result, module);

      if (log) {
        log->Printf("Interpreted a %s", inst->getOpcodeName());
        log->Printf("  Incoming value : %s",
                    frame.SummarizeValue(value).c_str());
      }
    } break;
    case Instruction::GetElementPtr: {
      const GetElementPtrInst *gep_inst = dyn_cast<GetElementPtrInst>(inst);

      if (!gep_inst) {
        if (log)
          log->Printf("getOpcode() returns GetElementPtr, but instruction is "
                      "not a GetElementPtrInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      const Value *pointer_operand = gep_inst->getPointerOperand();
      Type *src_elem_ty = gep_inst->getSourceElementType();

      lldb_private::Scalar P;

      if (!frame.EvaluateValue(P, pointer_operand, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s",
                      PrintValue(pointer_operand).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      typedef SmallVector<Value *, 8> IndexVector;
      typedef IndexVector::iterator IndexIterator;

      SmallVector<Value *, 8> indices(gep_inst->idx_begin(),
                                      gep_inst->idx_end());

      SmallVector<Value *, 8> const_indices;

      for (IndexIterator ii = indices.begin(), ie = indices.end(); ii != ie;
           ++ii) {
        ConstantInt *constant_index = dyn_cast<ConstantInt>(*ii);

        if (!constant_index) {
          lldb_private::Scalar I;

          if (!frame.EvaluateValue(I, *ii, module)) {
            if (log)
              log->Printf("Couldn't evaluate %s", PrintValue(*ii).c_str());
            error.SetErrorToGenericError();
            error.SetErrorString(bad_value_error);
            return false;
          }

          if (log)
            log->Printf("Evaluated constant index %s as %llu",
                        PrintValue(*ii).c_str(),
                        I.ULongLong(LLDB_INVALID_ADDRESS));

          constant_index = cast<ConstantInt>(ConstantInt::get(
              (*ii)->getType(), I.ULongLong(LLDB_INVALID_ADDRESS)));
        }

        const_indices.push_back(constant_index);
      }

      uint64_t offset =
          data_layout.getIndexedOffsetInType(src_elem_ty, const_indices);

      lldb_private::Scalar Poffset = P + offset;

      frame.AssignValue(inst, Poffset, module);

      if (log) {
        log->Printf("Interpreted a GetElementPtrInst");
        log->Printf("  P       : %s",
                    frame.SummarizeValue(pointer_operand).c_str());
        log->Printf("  Poffset : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::ICmp: {
      const ICmpInst *icmp_inst = dyn_cast<ICmpInst>(inst);

      if (!icmp_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns ICmp, but instruction is not an ICmpInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      CmpInst::Predicate predicate = icmp_inst->getPredicate();

      Value *lhs = inst->getOperand(0);
      Value *rhs = inst->getOperand(1);

      lldb_private::Scalar L;
      lldb_private::Scalar R;

      if (!frame.EvaluateValue(L, lhs, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(lhs).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      if (!frame.EvaluateValue(R, rhs, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(rhs).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      lldb_private::Scalar result;

      switch (predicate) {
      default:
        return false;
      case CmpInst::ICMP_EQ:
        result = (L == R);
        break;
      case CmpInst::ICMP_NE:
        result = (L != R);
        break;
      case CmpInst::ICMP_UGT:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = (L > R);
        break;
      case CmpInst::ICMP_UGE:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = (L >= R);
        break;
      case CmpInst::ICMP_ULT:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = (L < R);
        break;
      case CmpInst::ICMP_ULE:
        L.MakeUnsigned();
        R.MakeUnsigned();
        result = (L <= R);
        break;
      case CmpInst::ICMP_SGT:
        L.MakeSigned();
        R.MakeSigned();
        result = (L > R);
        break;
      case CmpInst::ICMP_SGE:
        L.MakeSigned();
        R.MakeSigned();
        result = (L >= R);
        break;
      case CmpInst::ICMP_SLT:
        L.MakeSigned();
        R.MakeSigned();
        result = (L < R);
        break;
      case CmpInst::ICMP_SLE:
        L.MakeSigned();
        R.MakeSigned();
        result = (L <= R);
        break;
      }

      frame.AssignValue(inst, result, module);

      if (log) {
        log->Printf("Interpreted an ICmpInst");
        log->Printf("  L : %s", frame.SummarizeValue(lhs).c_str());
        log->Printf("  R : %s", frame.SummarizeValue(rhs).c_str());
        log->Printf("  = : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::IntToPtr: {
      const IntToPtrInst *int_to_ptr_inst = dyn_cast<IntToPtrInst>(inst);

      if (!int_to_ptr_inst) {
        if (log)
          log->Printf("getOpcode() returns IntToPtr, but instruction is not an "
                      "IntToPtrInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *src_operand = int_to_ptr_inst->getOperand(0);

      lldb_private::Scalar I;

      if (!frame.EvaluateValue(I, src_operand, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(src_operand).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      frame.AssignValue(inst, I, module);

      if (log) {
        log->Printf("Interpreted an IntToPtr");
        log->Printf("  Src : %s", frame.SummarizeValue(src_operand).c_str());
        log->Printf("  =   : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::PtrToInt: {
      const PtrToIntInst *ptr_to_int_inst = dyn_cast<PtrToIntInst>(inst);

      if (!ptr_to_int_inst) {
        if (log)
          log->Printf("getOpcode() returns PtrToInt, but instruction is not an "
                      "PtrToIntInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *src_operand = ptr_to_int_inst->getOperand(0);

      lldb_private::Scalar I;

      if (!frame.EvaluateValue(I, src_operand, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(src_operand).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      frame.AssignValue(inst, I, module);

      if (log) {
        log->Printf("Interpreted a PtrToInt");
        log->Printf("  Src : %s", frame.SummarizeValue(src_operand).c_str());
        log->Printf("  =   : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::Trunc: {
      const TruncInst *trunc_inst = dyn_cast<TruncInst>(inst);

      if (!trunc_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns Trunc, but instruction is not a TruncInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      Value *src_operand = trunc_inst->getOperand(0);

      lldb_private::Scalar I;

      if (!frame.EvaluateValue(I, src_operand, module)) {
        if (log)
          log->Printf("Couldn't evaluate %s", PrintValue(src_operand).c_str());
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      frame.AssignValue(inst, I, module);

      if (log) {
        log->Printf("Interpreted a Trunc");
        log->Printf("  Src : %s", frame.SummarizeValue(src_operand).c_str());
        log->Printf("  =   : %s", frame.SummarizeValue(inst).c_str());
      }
    } break;
    case Instruction::Load: {
      const LoadInst *load_inst = dyn_cast<LoadInst>(inst);

      if (!load_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns Load, but instruction is not a LoadInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      // The semantics of Load are:
      //   Create a region D that will contain the loaded data
      //   Resolve the region P containing a pointer
      //   Dereference P to get the region R that the data should be loaded from
      //   Transfer a unit of type type(D) from R to D

      const Value *pointer_operand = load_inst->getPointerOperand();

      Type *pointer_ty = pointer_operand->getType();
      PointerType *pointer_ptr_ty = dyn_cast<PointerType>(pointer_ty);
      if (!pointer_ptr_ty) {
        if (log)
          log->Printf("getPointerOperand()->getType() is not a PointerType");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }
      Type *target_ty = pointer_ptr_ty->getElementType();

      lldb::addr_t D = frame.ResolveValue(load_inst, module);
      lldb::addr_t P = frame.ResolveValue(pointer_operand, module);

      if (D == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("LoadInst's value doesn't resolve to anything");
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      if (P == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("LoadInst's pointer doesn't resolve to anything");
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      lldb::addr_t R;
      lldb_private::Status read_error;
      execution_unit.ReadPointerFromMemory(&R, P, read_error);

      if (!read_error.Success()) {
        if (log)
          log->Printf("Couldn't read the address to be loaded for a LoadInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_read_error);
        return false;
      }

      size_t target_size = data_layout.getTypeStoreSize(target_ty);
      lldb_private::DataBufferHeap buffer(target_size, 0);

      read_error.Clear();
      execution_unit.ReadMemory(buffer.GetBytes(), R, buffer.GetByteSize(),
                                read_error);
      if (!read_error.Success()) {
        if (log)
          log->Printf("Couldn't read from a region on behalf of a LoadInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_read_error);
        return false;
      }

      lldb_private::Status write_error;
      execution_unit.WriteMemory(D, buffer.GetBytes(), buffer.GetByteSize(),
                                 write_error);
      if (!write_error.Success()) {
        if (log)
          log->Printf("Couldn't write to a region on behalf of a LoadInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_read_error);
        return false;
      }

      if (log) {
        log->Printf("Interpreted a LoadInst");
        log->Printf("  P : 0x%" PRIx64, P);
        log->Printf("  R : 0x%" PRIx64, R);
        log->Printf("  D : 0x%" PRIx64, D);
      }
    } break;
    case Instruction::Ret: {
      return true;
    }
    case Instruction::Store: {
      const StoreInst *store_inst = dyn_cast<StoreInst>(inst);

      if (!store_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns Store, but instruction is not a StoreInst");
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      // The semantics of Store are:
      //   Resolve the region D containing the data to be stored
      //   Resolve the region P containing a pointer
      //   Dereference P to get the region R that the data should be stored in
      //   Transfer a unit of type type(D) from D to R

      const Value *value_operand = store_inst->getValueOperand();
      const Value *pointer_operand = store_inst->getPointerOperand();

      Type *pointer_ty = pointer_operand->getType();
      PointerType *pointer_ptr_ty = dyn_cast<PointerType>(pointer_ty);
      if (!pointer_ptr_ty)
        return false;
      Type *target_ty = pointer_ptr_ty->getElementType();

      lldb::addr_t D = frame.ResolveValue(value_operand, module);
      lldb::addr_t P = frame.ResolveValue(pointer_operand, module);

      if (D == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("StoreInst's value doesn't resolve to anything");
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      if (P == LLDB_INVALID_ADDRESS) {
        if (log)
          log->Printf("StoreInst's pointer doesn't resolve to anything");
        error.SetErrorToGenericError();
        error.SetErrorString(bad_value_error);
        return false;
      }

      lldb::addr_t R;
      lldb_private::Status read_error;
      execution_unit.ReadPointerFromMemory(&R, P, read_error);

      if (!read_error.Success()) {
        if (log)
          log->Printf("Couldn't read the address to be loaded for a LoadInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_read_error);
        return false;
      }

      size_t target_size = data_layout.getTypeStoreSize(target_ty);
      lldb_private::DataBufferHeap buffer(target_size, 0);

      read_error.Clear();
      execution_unit.ReadMemory(buffer.GetBytes(), D, buffer.GetByteSize(),
                                read_error);
      if (!read_error.Success()) {
        if (log)
          log->Printf("Couldn't read from a region on behalf of a StoreInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_read_error);
        return false;
      }

      lldb_private::Status write_error;
      execution_unit.WriteMemory(R, buffer.GetBytes(), buffer.GetByteSize(),
                                 write_error);
      if (!write_error.Success()) {
        if (log)
          log->Printf("Couldn't write to a region on behalf of a StoreInst");
        error.SetErrorToGenericError();
        error.SetErrorString(memory_write_error);
        return false;
      }

      if (log) {
        log->Printf("Interpreted a StoreInst");
        log->Printf("  D : 0x%" PRIx64, D);
        log->Printf("  P : 0x%" PRIx64, P);
        log->Printf("  R : 0x%" PRIx64, R);
      }
    } break;
    case Instruction::Call: {
      const CallInst *call_inst = dyn_cast<CallInst>(inst);

      if (!call_inst) {
        if (log)
          log->Printf(
              "getOpcode() returns %s, but instruction is not a CallInst",
              inst->getOpcodeName());
        error.SetErrorToGenericError();
        error.SetErrorString(interpreter_internal_error);
        return false;
      }

      if (CanIgnoreCall(call_inst))
        break;

      // Get the return type
      llvm::Type *returnType = call_inst->getType();
      if (returnType == nullptr) {
        error.SetErrorToGenericError();
        error.SetErrorString("unable to access return type");
        return false;
      }

      // Work with void, integer and pointer return types
      if (!returnType->isVoidTy() && !returnType->isIntegerTy() &&
          !returnType->isPointerTy()) {
        error.SetErrorToGenericError();
        error.SetErrorString("return type is not supported");
        return false;
      }

      // Check we can actually get a thread
      if (exe_ctx.GetThreadPtr() == nullptr) {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("unable to acquire thread");
        return false;
      }

      // Make sure we have a valid process
      if (!exe_ctx.GetProcessPtr()) {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("unable to get the process");
        return false;
      }

      // Find the address of the callee function
      lldb_private::Scalar I;
      const llvm::Value *val = call_inst->getCalledValue();

      if (!frame.EvaluateValue(I, val, module)) {
        error.SetErrorToGenericError();
        error.SetErrorString("unable to get address of function");
        return false;
      }
      lldb_private::Address funcAddr(I.ULongLong(LLDB_INVALID_ADDRESS));

      lldb_private::DiagnosticManager diagnostics;
      lldb_private::EvaluateExpressionOptions options;

      // We generally receive a function pointer which we must dereference
      llvm::Type *prototype = val->getType();
      if (!prototype->isPointerTy()) {
        error.SetErrorToGenericError();
        error.SetErrorString("call need function pointer");
        return false;
      }

      // Dereference the function pointer
      prototype = prototype->getPointerElementType();
      if (!(prototype->isFunctionTy() || prototype->isFunctionVarArg())) {
        error.SetErrorToGenericError();
        error.SetErrorString("call need function pointer");
        return false;
      }

      // Find number of arguments
      const int numArgs = call_inst->getNumArgOperands();

      // We work with a fixed array of 16 arguments which is our upper limit
      static lldb_private::ABI::CallArgument rawArgs[16];
      if (numArgs >= 16) {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("function takes too many arguments");
        return false;
      }

      // Push all function arguments to the argument list that will be passed
      // to the call function thread plan
      for (int i = 0; i < numArgs; i++) {
        // Get details of this argument
        llvm::Value *arg_op = call_inst->getArgOperand(i);
        llvm::Type *arg_ty = arg_op->getType();

        // Ensure that this argument is an supported type
        if (!arg_ty->isIntegerTy() && !arg_ty->isPointerTy()) {
          error.SetErrorToGenericError();
          error.SetErrorStringWithFormat("argument %d must be integer type", i);
          return false;
        }

        // Extract the arguments value
        lldb_private::Scalar tmp_op = 0;
        if (!frame.EvaluateValue(tmp_op, arg_op, module)) {
          error.SetErrorToGenericError();
          error.SetErrorStringWithFormat("unable to evaluate argument %d", i);
          return false;
        }

        // Check if this is a string literal or constant string pointer
        if (arg_ty->isPointerTy()) {
          lldb::addr_t addr = tmp_op.ULongLong();
          size_t dataSize = 0;

          bool Success = execution_unit.GetAllocSize(addr, dataSize);
          (void)Success;
          assert(Success &&
                 "unable to locate host data for transfer to device");
          // Create the required buffer
          rawArgs[i].size = dataSize;
          rawArgs[i].data_ap.reset(new uint8_t[dataSize + 1]);

          // Read string from host memory
          execution_unit.ReadMemory(rawArgs[i].data_ap.get(), addr, dataSize,
                                    error);
          assert(!error.Fail() &&
                 "we have failed to read the string from memory");

          // Add null terminator
          rawArgs[i].data_ap[dataSize] = '\0';
          rawArgs[i].type = lldb_private::ABI::CallArgument::HostPointer;
        } else /* if ( arg_ty->isPointerTy() ) */
        {
          rawArgs[i].type = lldb_private::ABI::CallArgument::TargetValue;
          // Get argument size in bytes
          rawArgs[i].size = arg_ty->getIntegerBitWidth() / 8;
          // Push value into argument list for thread plan
          rawArgs[i].value = tmp_op.ULongLong();
        }
      }

      // Pack the arguments into an llvm::array
      llvm::ArrayRef<lldb_private::ABI::CallArgument> args(rawArgs, numArgs);

      // Setup a thread plan to call the target function
      lldb::ThreadPlanSP call_plan_sp(
          new lldb_private::ThreadPlanCallFunctionUsingABI(
              exe_ctx.GetThreadRef(), funcAddr, *prototype, *returnType, args,
              options));

      // Check if the plan is valid
      lldb_private::StreamString ss;
      if (!call_plan_sp || !call_plan_sp->ValidatePlan(&ss)) {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat(
            "unable to make ThreadPlanCallFunctionUsingABI for 0x%llx",
            I.ULongLong());
        return false;
      }

      exe_ctx.GetProcessPtr()->SetRunningUserExpression(true);

      // Execute the actual function call thread plan
      lldb::ExpressionResults res = exe_ctx.GetProcessRef().RunThreadPlan(
          exe_ctx, call_plan_sp, options, diagnostics);

      // Check that the thread plan completed successfully
      if (res != lldb::ExpressionResults::eExpressionCompleted) {
        error.SetErrorToGenericError();
        error.SetErrorStringWithFormat("ThreadPlanCallFunctionUsingABI failed");
        return false;
      }

      exe_ctx.GetProcessPtr()->SetRunningUserExpression(false);

      // Void return type
      if (returnType->isVoidTy()) {
        // Cant assign to void types, so we leave the frame untouched
      } else
          // Integer or pointer return type
          if (returnType->isIntegerTy() || returnType->isPointerTy()) {
        // Get the encapsulated return value
        lldb::ValueObjectSP retVal = call_plan_sp.get()->GetReturnValueObject();

        lldb_private::Scalar returnVal = -1;
        lldb_private::ValueObject *vobj = retVal.get();

        // Check if the return value is valid
        if (vobj == nullptr || retVal.empty()) {
          error.SetErrorToGenericError();
          error.SetErrorStringWithFormat("unable to get the return value");
          return false;
        }

        // Extract the return value as a integer
        lldb_private::Value &value = vobj->GetValue();
        returnVal = value.GetScalar();

        // Push the return value as the result
        frame.AssignValue(inst, returnVal, module);
      }
    } break;
    }

    ++frame.m_ii;
  }

  if (num_insts >= 4096) {
    error.SetErrorToGenericError();
    error.SetErrorString(infinite_loop_error);
    return false;
  }

  return false;
}

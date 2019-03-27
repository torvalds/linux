//===-- ObjCLanguageRuntime.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/Type.h"

#include "lldb/Core/MappedHash.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DJB.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
ObjCLanguageRuntime::~ObjCLanguageRuntime() {}

ObjCLanguageRuntime::ObjCLanguageRuntime(Process *process)
    : LanguageRuntime(process), m_impl_cache(),
      m_has_new_literals_and_indexing(eLazyBoolCalculate),
      m_isa_to_descriptor(), m_hash_to_isa_map(), m_type_size_cache(),
      m_isa_to_descriptor_stop_id(UINT32_MAX), m_complete_class_cache(),
      m_negative_complete_class_cache() {}

bool ObjCLanguageRuntime::AddClass(ObjCISA isa,
                                   const ClassDescriptorSP &descriptor_sp,
                                   const char *class_name) {
  if (isa != 0) {
    m_isa_to_descriptor[isa] = descriptor_sp;
    // class_name is assumed to be valid
    m_hash_to_isa_map.insert(std::make_pair(llvm::djbHash(class_name), isa));
    return true;
  }
  return false;
}

void ObjCLanguageRuntime::AddToMethodCache(lldb::addr_t class_addr,
                                           lldb::addr_t selector,
                                           lldb::addr_t impl_addr) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_STEP));
  if (log) {
    log->Printf("Caching: class 0x%" PRIx64 " selector 0x%" PRIx64
                " implementation 0x%" PRIx64 ".",
                class_addr, selector, impl_addr);
  }
  m_impl_cache.insert(std::pair<ClassAndSel, lldb::addr_t>(
      ClassAndSel(class_addr, selector), impl_addr));
}

lldb::addr_t ObjCLanguageRuntime::LookupInMethodCache(lldb::addr_t class_addr,
                                                      lldb::addr_t selector) {
  MsgImplMap::iterator pos, end = m_impl_cache.end();
  pos = m_impl_cache.find(ClassAndSel(class_addr, selector));
  if (pos != end)
    return (*pos).second;
  return LLDB_INVALID_ADDRESS;
}

lldb::TypeSP
ObjCLanguageRuntime::LookupInCompleteClassCache(ConstString &name) {
  CompleteClassMap::iterator complete_class_iter =
      m_complete_class_cache.find(name);

  if (complete_class_iter != m_complete_class_cache.end()) {
    // Check the weak pointer to make sure the type hasn't been unloaded
    TypeSP complete_type_sp(complete_class_iter->second.lock());

    if (complete_type_sp)
      return complete_type_sp;
    else
      m_complete_class_cache.erase(name);
  }

  if (m_negative_complete_class_cache.count(name) > 0)
    return TypeSP();

  const ModuleList &modules = m_process->GetTarget().GetImages();

  SymbolContextList sc_list;
  const size_t matching_symbols =
      modules.FindSymbolsWithNameAndType(name, eSymbolTypeObjCClass, sc_list);

  if (matching_symbols) {
    SymbolContext sc;

    sc_list.GetContextAtIndex(0, sc);

    ModuleSP module_sp(sc.module_sp);

    if (!module_sp)
      return TypeSP();

    const bool exact_match = true;
    const uint32_t max_matches = UINT32_MAX;
    TypeList types;

    llvm::DenseSet<SymbolFile *> searched_symbol_files;
    const uint32_t num_types = module_sp->FindTypes(
        name, exact_match, max_matches, searched_symbol_files, types);

    if (num_types) {
      uint32_t i;
      for (i = 0; i < num_types; ++i) {
        TypeSP type_sp(types.GetTypeAtIndex(i));

        if (ClangASTContext::IsObjCObjectOrInterfaceType(
                type_sp->GetForwardCompilerType())) {
          if (type_sp->IsCompleteObjCClass()) {
            m_complete_class_cache[name] = type_sp;
            return type_sp;
          }
        }
      }
    }
  }
  m_negative_complete_class_cache.insert(name);
  return TypeSP();
}

size_t ObjCLanguageRuntime::GetByteOffsetForIvar(CompilerType &parent_qual_type,
                                                 const char *ivar_name) {
  return LLDB_INVALID_IVAR_OFFSET;
}

bool ObjCLanguageRuntime::ClassDescriptor::IsPointerValid(
    lldb::addr_t value, uint32_t ptr_size, bool allow_NULLs, bool allow_tagged,
    bool check_version_specific) const {
  if (!value)
    return allow_NULLs;
  if ((value % 2) == 1 && allow_tagged)
    return true;
  if ((value % ptr_size) == 0)
    return (check_version_specific ? CheckPointer(value, ptr_size) : true);
  else
    return false;
}

ObjCLanguageRuntime::ObjCISA
ObjCLanguageRuntime::GetISA(const ConstString &name) {
  ISAToDescriptorIterator pos = GetDescriptorIterator(name);
  if (pos != m_isa_to_descriptor.end())
    return pos->first;
  return 0;
}

ObjCLanguageRuntime::ISAToDescriptorIterator
ObjCLanguageRuntime::GetDescriptorIterator(const ConstString &name) {
  ISAToDescriptorIterator end = m_isa_to_descriptor.end();

  if (name) {
    UpdateISAToDescriptorMap();
    if (m_hash_to_isa_map.empty()) {
      // No name hashes were provided, we need to just linearly power through
      // the names and find a match
      for (ISAToDescriptorIterator pos = m_isa_to_descriptor.begin();
           pos != end; ++pos) {
        if (pos->second->GetClassName() == name)
          return pos;
      }
    } else {
      // Name hashes were provided, so use them to efficiently lookup name to
      // isa/descriptor
      const uint32_t name_hash = llvm::djbHash(name.GetStringRef());
      std::pair<HashToISAIterator, HashToISAIterator> range =
          m_hash_to_isa_map.equal_range(name_hash);
      for (HashToISAIterator range_pos = range.first; range_pos != range.second;
           ++range_pos) {
        ISAToDescriptorIterator pos =
            m_isa_to_descriptor.find(range_pos->second);
        if (pos != m_isa_to_descriptor.end()) {
          if (pos->second->GetClassName() == name)
            return pos;
        }
      }
    }
  }
  return end;
}

std::pair<ObjCLanguageRuntime::ISAToDescriptorIterator,
          ObjCLanguageRuntime::ISAToDescriptorIterator>
ObjCLanguageRuntime::GetDescriptorIteratorPair(bool update_if_needed) {
  if (update_if_needed)
    UpdateISAToDescriptorMapIfNeeded();

  return std::pair<ObjCLanguageRuntime::ISAToDescriptorIterator,
                   ObjCLanguageRuntime::ISAToDescriptorIterator>(
      m_isa_to_descriptor.begin(), m_isa_to_descriptor.end());
}

ObjCLanguageRuntime::ObjCISA
ObjCLanguageRuntime::GetParentClass(ObjCLanguageRuntime::ObjCISA isa) {
  ClassDescriptorSP objc_class_sp(GetClassDescriptorFromISA(isa));
  if (objc_class_sp) {
    ClassDescriptorSP objc_super_class_sp(objc_class_sp->GetSuperclass());
    if (objc_super_class_sp)
      return objc_super_class_sp->GetISA();
  }
  return 0;
}

ConstString
ObjCLanguageRuntime::GetActualTypeName(ObjCLanguageRuntime::ObjCISA isa) {
  ClassDescriptorSP objc_class_sp(GetNonKVOClassDescriptor(isa));
  if (objc_class_sp)
    return objc_class_sp->GetClassName();
  return ConstString();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptorFromClassName(
    const ConstString &class_name) {
  ISAToDescriptorIterator pos = GetDescriptorIterator(class_name);
  if (pos != m_isa_to_descriptor.end())
    return pos->second;
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptor(ValueObject &valobj) {
  ClassDescriptorSP objc_class_sp;
  // if we get an invalid VO (which might still happen when playing around with
  // pointers returned by the expression parser, don't consider this a valid
  // ObjC object)
  if (valobj.GetCompilerType().IsValid()) {
    addr_t isa_pointer = valobj.GetPointerValue();
    if (isa_pointer != LLDB_INVALID_ADDRESS) {
      ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

      Process *process = exe_ctx.GetProcessPtr();
      if (process) {
        Status error;
        ObjCISA isa = process->ReadPointerFromMemory(isa_pointer, error);
        if (isa != LLDB_INVALID_ADDRESS)
          objc_class_sp = GetClassDescriptorFromISA(isa);
      }
    }
  }
  return objc_class_sp;
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetNonKVOClassDescriptor(ValueObject &valobj) {
  ObjCLanguageRuntime::ClassDescriptorSP objc_class_sp(
      GetClassDescriptor(valobj));
  if (objc_class_sp) {
    if (!objc_class_sp->IsKVO())
      return objc_class_sp;

    ClassDescriptorSP non_kvo_objc_class_sp(objc_class_sp->GetSuperclass());
    if (non_kvo_objc_class_sp && non_kvo_objc_class_sp->IsValid())
      return non_kvo_objc_class_sp;
  }
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetClassDescriptorFromISA(ObjCISA isa) {
  if (isa) {
    UpdateISAToDescriptorMap();
    ObjCLanguageRuntime::ISAToDescriptorIterator pos =
        m_isa_to_descriptor.find(isa);
    if (pos != m_isa_to_descriptor.end())
      return pos->second;
  }
  return ClassDescriptorSP();
}

ObjCLanguageRuntime::ClassDescriptorSP
ObjCLanguageRuntime::GetNonKVOClassDescriptor(ObjCISA isa) {
  if (isa) {
    ClassDescriptorSP objc_class_sp = GetClassDescriptorFromISA(isa);
    if (objc_class_sp && objc_class_sp->IsValid()) {
      if (!objc_class_sp->IsKVO())
        return objc_class_sp;

      ClassDescriptorSP non_kvo_objc_class_sp(objc_class_sp->GetSuperclass());
      if (non_kvo_objc_class_sp && non_kvo_objc_class_sp->IsValid())
        return non_kvo_objc_class_sp;
    }
  }
  return ClassDescriptorSP();
}

CompilerType
ObjCLanguageRuntime::EncodingToType::RealizeType(const char *name,
                                                 bool for_expression) {
  if (m_scratch_ast_ctx_ap)
    return RealizeType(*m_scratch_ast_ctx_ap, name, for_expression);
  return CompilerType();
}

CompilerType ObjCLanguageRuntime::EncodingToType::RealizeType(
    ClangASTContext &ast_ctx, const char *name, bool for_expression) {
  clang::ASTContext *clang_ast = ast_ctx.getASTContext();
  if (!clang_ast)
    return CompilerType();
  return RealizeType(*clang_ast, name, for_expression);
}

ObjCLanguageRuntime::EncodingToType::~EncodingToType() {}

ObjCLanguageRuntime::EncodingToTypeSP ObjCLanguageRuntime::GetEncodingToType() {
  return nullptr;
}

bool ObjCLanguageRuntime::GetTypeBitSize(const CompilerType &compiler_type,
                                         uint64_t &size) {
  void *opaque_ptr = compiler_type.GetOpaqueQualType();
  size = m_type_size_cache.Lookup(opaque_ptr);
  // an ObjC object will at least have an ISA, so 0 is definitely not OK
  if (size > 0)
    return true;

  ClassDescriptorSP class_descriptor_sp =
      GetClassDescriptorFromClassName(compiler_type.GetTypeName());
  if (!class_descriptor_sp)
    return false;

  int32_t max_offset = INT32_MIN;
  uint64_t sizeof_max = 0;
  bool found = false;

  for (size_t idx = 0; idx < class_descriptor_sp->GetNumIVars(); idx++) {
    const auto &ivar = class_descriptor_sp->GetIVarAtIndex(idx);
    int32_t cur_offset = ivar.m_offset;
    if (cur_offset > max_offset) {
      max_offset = cur_offset;
      sizeof_max = ivar.m_size;
      found = true;
    }
  }

  size = 8 * (max_offset + sizeof_max);
  if (found)
    m_type_size_cache.Insert(opaque_ptr, size);

  return found;
}

//------------------------------------------------------------------
// Exception breakpoint Precondition class for ObjC:
//------------------------------------------------------------------
void ObjCLanguageRuntime::ObjCExceptionPrecondition::AddClassName(
    const char *class_name) {
  m_class_names.insert(class_name);
}

ObjCLanguageRuntime::ObjCExceptionPrecondition::ObjCExceptionPrecondition() {}

bool ObjCLanguageRuntime::ObjCExceptionPrecondition::EvaluatePrecondition(
    StoppointCallbackContext &context) {
  return true;
}

void ObjCLanguageRuntime::ObjCExceptionPrecondition::GetDescription(
    Stream &stream, lldb::DescriptionLevel level) {}

Status ObjCLanguageRuntime::ObjCExceptionPrecondition::ConfigurePrecondition(
    Args &args) {
  Status error;
  if (args.GetArgumentCount() > 0)
    error.SetErrorString(
        "The ObjC Exception breakpoint doesn't support extra options.");
  return error;
}

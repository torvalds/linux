//===-- Materializer.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/Materializer.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectVariable.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"

using namespace lldb_private;

uint32_t Materializer::AddStructMember(Entity &entity) {
  uint32_t size = entity.GetSize();
  uint32_t alignment = entity.GetAlignment();

  uint32_t ret;

  if (m_current_offset == 0)
    m_struct_alignment = alignment;

  if (m_current_offset % alignment)
    m_current_offset += (alignment - (m_current_offset % alignment));

  ret = m_current_offset;

  m_current_offset += size;

  return ret;
}

void Materializer::Entity::SetSizeAndAlignmentFromType(CompilerType &type) {
  if (llvm::Optional<uint64_t> size = type.GetByteSize(nullptr))
    m_size = *size;

  uint32_t bit_alignment = type.GetTypeBitAlign();

  if (bit_alignment % 8) {
    bit_alignment += 8;
    bit_alignment &= ~((uint32_t)0x111u);
  }

  m_alignment = bit_alignment / 8;
}

class EntityPersistentVariable : public Materializer::Entity {
public:
  EntityPersistentVariable(lldb::ExpressionVariableSP &persistent_variable_sp,
                           Materializer::PersistentVariableDelegate *delegate)
      : Entity(), m_persistent_variable_sp(persistent_variable_sp),
        m_delegate(delegate) {
    // Hard-coding to maximum size of a pointer since persistent variables are
    // materialized by reference
    m_size = 8;
    m_alignment = 8;
  }

  void MakeAllocation(IRMemoryMap &map, Status &err) {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    // Allocate a spare memory area to store the persistent variable's
    // contents.

    Status allocate_error;
    const bool zero_memory = false;

    lldb::addr_t mem = map.Malloc(
        m_persistent_variable_sp->GetByteSize(), 8,
        lldb::ePermissionsReadable | lldb::ePermissionsWritable,
        IRMemoryMap::eAllocationPolicyMirror, zero_memory, allocate_error);

    if (!allocate_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't allocate a memory area to store %s: %s",
          m_persistent_variable_sp->GetName().GetCString(),
          allocate_error.AsCString());
      return;
    }

    if (log)
      log->Printf("Allocated %s (0x%" PRIx64 ") successfully",
                  m_persistent_variable_sp->GetName().GetCString(), mem);

    // Put the location of the spare memory into the live data of the
    // ValueObject.

    m_persistent_variable_sp->m_live_sp = ValueObjectConstResult::Create(
        map.GetBestExecutionContextScope(),
        m_persistent_variable_sp->GetCompilerType(),
        m_persistent_variable_sp->GetName(), mem, eAddressTypeLoad,
        map.GetAddressByteSize());

    // Clear the flag if the variable will never be deallocated.

    if (m_persistent_variable_sp->m_flags &
        ExpressionVariable::EVKeepInTarget) {
      Status leak_error;
      map.Leak(mem, leak_error);
      m_persistent_variable_sp->m_flags &=
          ~ExpressionVariable::EVNeedsAllocation;
    }

    // Write the contents of the variable to the area.

    Status write_error;

    map.WriteMemory(mem, m_persistent_variable_sp->GetValueBytes(),
                    m_persistent_variable_sp->GetByteSize(), write_error);

    if (!write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write %s to the target: %s",
          m_persistent_variable_sp->GetName().AsCString(),
          write_error.AsCString());
      return;
    }
  }

  void DestroyAllocation(IRMemoryMap &map, Status &err) {
    Status deallocate_error;

    map.Free((lldb::addr_t)m_persistent_variable_sp->m_live_sp->GetValue()
                 .GetScalar()
                 .ULongLong(),
             deallocate_error);

    m_persistent_variable_sp->m_live_sp.reset();

    if (!deallocate_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't deallocate memory for %s: %s",
          m_persistent_variable_sp->GetName().GetCString(),
          deallocate_error.AsCString());
    }
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf("EntityPersistentVariable::Materialize [address = 0x%" PRIx64
                  ", m_name = %s, m_flags = 0x%hx]",
                  (uint64_t)load_addr,
                  m_persistent_variable_sp->GetName().AsCString(),
                  m_persistent_variable_sp->m_flags);
    }

    if (m_persistent_variable_sp->m_flags &
        ExpressionVariable::EVNeedsAllocation) {
      MakeAllocation(map, err);
      m_persistent_variable_sp->m_flags |=
          ExpressionVariable::EVIsLLDBAllocated;

      if (!err.Success())
        return;
    }

    if ((m_persistent_variable_sp->m_flags &
             ExpressionVariable::EVIsProgramReference &&
         m_persistent_variable_sp->m_live_sp) ||
        m_persistent_variable_sp->m_flags &
            ExpressionVariable::EVIsLLDBAllocated) {
      Status write_error;

      map.WriteScalarToMemory(
          load_addr,
          m_persistent_variable_sp->m_live_sp->GetValue().GetScalar(),
          map.GetAddressByteSize(), write_error);

      if (!write_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't write the location of %s to memory: %s",
            m_persistent_variable_sp->GetName().AsCString(),
            write_error.AsCString());
      }
    } else {
      err.SetErrorStringWithFormat(
          "no materialization happened for persistent variable %s",
          m_persistent_variable_sp->GetName().AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf(
          "EntityPersistentVariable::Dematerialize [address = 0x%" PRIx64
          ", m_name = %s, m_flags = 0x%hx]",
          (uint64_t)process_address + m_offset,
          m_persistent_variable_sp->GetName().AsCString(),
          m_persistent_variable_sp->m_flags);
    }

    if (m_delegate) {
      m_delegate->DidDematerialize(m_persistent_variable_sp);
    }

    if ((m_persistent_variable_sp->m_flags &
         ExpressionVariable::EVIsLLDBAllocated) ||
        (m_persistent_variable_sp->m_flags &
         ExpressionVariable::EVIsProgramReference)) {
      if (m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVIsProgramReference &&
          !m_persistent_variable_sp->m_live_sp) {
        // If the reference comes from the program, then the
        // ClangExpressionVariable's live variable data hasn't been set up yet.
        // Do this now.

        lldb::addr_t location;
        Status read_error;

        map.ReadPointerFromMemory(&location, load_addr, read_error);

        if (!read_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't read the address of program-allocated variable %s: %s",
              m_persistent_variable_sp->GetName().GetCString(),
              read_error.AsCString());
          return;
        }

        m_persistent_variable_sp->m_live_sp = ValueObjectConstResult::Create(
            map.GetBestExecutionContextScope(),
            m_persistent_variable_sp.get()->GetCompilerType(),
            m_persistent_variable_sp->GetName(), location, eAddressTypeLoad,
            m_persistent_variable_sp->GetByteSize());

        if (frame_top != LLDB_INVALID_ADDRESS &&
            frame_bottom != LLDB_INVALID_ADDRESS && location >= frame_bottom &&
            location <= frame_top) {
          // If the variable is resident in the stack frame created by the
          // expression, then it cannot be relied upon to stay around.  We
          // treat it as needing reallocation.
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVIsLLDBAllocated;
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVNeedsAllocation;
          m_persistent_variable_sp->m_flags |=
              ExpressionVariable::EVNeedsFreezeDry;
          m_persistent_variable_sp->m_flags &=
              ~ExpressionVariable::EVIsProgramReference;
        }
      }

      lldb::addr_t mem = m_persistent_variable_sp->m_live_sp->GetValue()
                             .GetScalar()
                             .ULongLong();

      if (!m_persistent_variable_sp->m_live_sp) {
        err.SetErrorStringWithFormat(
            "couldn't find the memory area used to store %s",
            m_persistent_variable_sp->GetName().GetCString());
        return;
      }

      if (m_persistent_variable_sp->m_live_sp->GetValue()
              .GetValueAddressType() != eAddressTypeLoad) {
        err.SetErrorStringWithFormat(
            "the address of the memory area for %s is in an incorrect format",
            m_persistent_variable_sp->GetName().GetCString());
        return;
      }

      if (m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVNeedsFreezeDry ||
          m_persistent_variable_sp->m_flags &
              ExpressionVariable::EVKeepInTarget) {
        if (log)
          log->Printf(
              "Dematerializing %s from 0x%" PRIx64 " (size = %llu)",
              m_persistent_variable_sp->GetName().GetCString(), (uint64_t)mem,
              (unsigned long long)m_persistent_variable_sp->GetByteSize());

        // Read the contents of the spare memory area

        m_persistent_variable_sp->ValueUpdated();

        Status read_error;

        map.ReadMemory(m_persistent_variable_sp->GetValueBytes(), mem,
                       m_persistent_variable_sp->GetByteSize(), read_error);

        if (!read_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't read the contents of %s from memory: %s",
              m_persistent_variable_sp->GetName().GetCString(),
              read_error.AsCString());
          return;
        }

        m_persistent_variable_sp->m_flags &=
            ~ExpressionVariable::EVNeedsFreezeDry;
      }
    } else {
      err.SetErrorStringWithFormat(
          "no dematerialization happened for persistent variable %s",
          m_persistent_variable_sp->GetName().AsCString());
      return;
    }

    lldb::ProcessSP process_sp =
        map.GetBestExecutionContextScope()->CalculateProcess();
    if (!process_sp || !process_sp->CanJIT()) {
      // Allocations are not persistent so persistent variables cannot stay
      // materialized.

      m_persistent_variable_sp->m_flags |=
          ExpressionVariable::EVNeedsAllocation;

      DestroyAllocation(map, err);
      if (!err.Success())
        return;
    } else if (m_persistent_variable_sp->m_flags &
                   ExpressionVariable::EVNeedsAllocation &&
               !(m_persistent_variable_sp->m_flags &
                 ExpressionVariable::EVKeepInTarget)) {
      DestroyAllocation(map, err);
      if (!err.Success())
        return;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityPersistentVariable (%s)\n",
                       load_addr,
                       m_persistent_variable_sp->GetName().AsCString());

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    {
      dump_stream.Printf("Target:\n");

      lldb::addr_t target_address;

      map.ReadPointerFromMemory(&target_address, load_addr, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataBufferHeap data(m_persistent_variable_sp->GetByteSize(), 0);

        map.ReadMemory(data.GetBytes(), target_address,
                       m_persistent_variable_sp->GetByteSize(), err);

        if (!err.Success()) {
          dump_stream.Printf("  <could not be read>\n");
        } else {
          DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                       target_address);

          dump_stream.PutChar('\n');
        }
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  lldb::ExpressionVariableSP m_persistent_variable_sp;
  Materializer::PersistentVariableDelegate *m_delegate;
};

uint32_t Materializer::AddPersistentVariable(
    lldb::ExpressionVariableSP &persistent_variable_sp,
    PersistentVariableDelegate *delegate, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  iter->reset(new EntityPersistentVariable(persistent_variable_sp, delegate));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntityVariable : public Materializer::Entity {
public:
  EntityVariable(lldb::VariableSP &variable_sp)
      : Entity(), m_variable_sp(variable_sp), m_is_reference(false),
        m_temporary_allocation(LLDB_INVALID_ADDRESS),
        m_temporary_allocation_size(0) {
    // Hard-coding to maximum size of a pointer since all variables are
    // materialized by reference
    m_size = 8;
    m_alignment = 8;
    m_is_reference =
        m_variable_sp->GetType()->GetForwardCompilerType().IsReferenceType();
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;
    if (log) {
      log->Printf("EntityVariable::Materialize [address = 0x%" PRIx64
                  ", m_variable_sp = %s]",
                  (uint64_t)load_addr, m_variable_sp->GetName().AsCString());
    }

    ExecutionContextScope *scope = frame_sp.get();

    if (!scope)
      scope = map.GetBestExecutionContextScope();

    lldb::ValueObjectSP valobj_sp =
        ValueObjectVariable::Create(scope, m_variable_sp);

    if (!valobj_sp) {
      err.SetErrorStringWithFormat(
          "couldn't get a value object for variable %s",
          m_variable_sp->GetName().AsCString());
      return;
    }

    Status valobj_error = valobj_sp->GetError();

    if (valobj_error.Fail()) {
      err.SetErrorStringWithFormat("couldn't get the value of variable %s: %s",
                                   m_variable_sp->GetName().AsCString(),
                                   valobj_error.AsCString());
      return;
    }

    if (m_is_reference) {
      DataExtractor valobj_extractor;
      Status extract_error;
      valobj_sp->GetData(valobj_extractor, extract_error);

      if (!extract_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't read contents of reference variable %s: %s",
            m_variable_sp->GetName().AsCString(), extract_error.AsCString());
        return;
      }

      lldb::offset_t offset = 0;
      lldb::addr_t reference_addr = valobj_extractor.GetAddress(&offset);

      Status write_error;
      map.WritePointerToMemory(load_addr, reference_addr, write_error);

      if (!write_error.Success()) {
        err.SetErrorStringWithFormat("couldn't write the contents of reference "
                                     "variable %s to memory: %s",
                                     m_variable_sp->GetName().AsCString(),
                                     write_error.AsCString());
        return;
      }
    } else {
      AddressType address_type = eAddressTypeInvalid;
      const bool scalar_is_load_address = false;
      lldb::addr_t addr_of_valobj =
          valobj_sp->GetAddressOf(scalar_is_load_address, &address_type);
      if (addr_of_valobj != LLDB_INVALID_ADDRESS) {
        Status write_error;
        map.WritePointerToMemory(load_addr, addr_of_valobj, write_error);

        if (!write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the address of variable %s to memory: %s",
              m_variable_sp->GetName().AsCString(), write_error.AsCString());
          return;
        }
      } else {
        DataExtractor data;
        Status extract_error;
        valobj_sp->GetData(data, extract_error);
        if (!extract_error.Success()) {
          err.SetErrorStringWithFormat("couldn't get the value of %s: %s",
                                       m_variable_sp->GetName().AsCString(),
                                       extract_error.AsCString());
          return;
        }

        if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
          err.SetErrorStringWithFormat(
              "trying to create a temporary region for %s but one exists",
              m_variable_sp->GetName().AsCString());
          return;
        }

        if (data.GetByteSize() < m_variable_sp->GetType()->GetByteSize()) {
          if (data.GetByteSize() == 0 &&
              !m_variable_sp->LocationExpression().IsValid()) {
            err.SetErrorStringWithFormat("the variable '%s' has no location, "
                                         "it may have been optimized out",
                                         m_variable_sp->GetName().AsCString());
          } else {
            err.SetErrorStringWithFormat(
                "size of variable %s (%" PRIu64
                ") is larger than the ValueObject's size (%" PRIu64 ")",
                m_variable_sp->GetName().AsCString(),
                m_variable_sp->GetType()->GetByteSize(), data.GetByteSize());
          }
          return;
        }

        size_t bit_align =
            m_variable_sp->GetType()->GetLayoutCompilerType().GetTypeBitAlign();
        size_t byte_align = (bit_align + 7) / 8;

        if (!byte_align)
          byte_align = 1;

        Status alloc_error;
        const bool zero_memory = false;

        m_temporary_allocation = map.Malloc(
            data.GetByteSize(), byte_align,
            lldb::ePermissionsReadable | lldb::ePermissionsWritable,
            IRMemoryMap::eAllocationPolicyMirror, zero_memory, alloc_error);

        m_temporary_allocation_size = data.GetByteSize();

        m_original_data.reset(
            new DataBufferHeap(data.GetDataStart(), data.GetByteSize()));

        if (!alloc_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't allocate a temporary region for %s: %s",
              m_variable_sp->GetName().AsCString(), alloc_error.AsCString());
          return;
        }

        Status write_error;

        map.WriteMemory(m_temporary_allocation, data.GetDataStart(),
                        data.GetByteSize(), write_error);

        if (!write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write to the temporary region for %s: %s",
              m_variable_sp->GetName().AsCString(), write_error.AsCString());
          return;
        }

        Status pointer_write_error;

        map.WritePointerToMemory(load_addr, m_temporary_allocation,
                                 pointer_write_error);

        if (!pointer_write_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the address of the temporary region for %s: %s",
              m_variable_sp->GetName().AsCString(),
              pointer_write_error.AsCString());
        }
      }
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;
    if (log) {
      log->Printf("EntityVariable::Dematerialize [address = 0x%" PRIx64
                  ", m_variable_sp = %s]",
                  (uint64_t)load_addr, m_variable_sp->GetName().AsCString());
    }

    if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      ExecutionContextScope *scope = frame_sp.get();

      if (!scope)
        scope = map.GetBestExecutionContextScope();

      lldb::ValueObjectSP valobj_sp =
          ValueObjectVariable::Create(scope, m_variable_sp);

      if (!valobj_sp) {
        err.SetErrorStringWithFormat(
            "couldn't get a value object for variable %s",
            m_variable_sp->GetName().AsCString());
        return;
      }

      lldb_private::DataExtractor data;

      Status extract_error;

      map.GetMemoryData(data, m_temporary_allocation, valobj_sp->GetByteSize(),
                        extract_error);

      if (!extract_error.Success()) {
        err.SetErrorStringWithFormat("couldn't get the data for variable %s",
                                     m_variable_sp->GetName().AsCString());
        return;
      }

      bool actually_write = true;

      if (m_original_data) {
        if ((data.GetByteSize() == m_original_data->GetByteSize()) &&
            !memcmp(m_original_data->GetBytes(), data.GetDataStart(),
                    data.GetByteSize())) {
          actually_write = false;
        }
      }

      Status set_error;

      if (actually_write) {
        valobj_sp->SetData(data, set_error);

        if (!set_error.Success()) {
          err.SetErrorStringWithFormat(
              "couldn't write the new contents of %s back into the variable",
              m_variable_sp->GetName().AsCString());
          return;
        }
      }

      Status free_error;

      map.Free(m_temporary_allocation, free_error);

      if (!free_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't free the temporary region for %s: %s",
            m_variable_sp->GetName().AsCString(), free_error.AsCString());
        return;
      }

      m_original_data.reset();
      m_temporary_allocation = LLDB_INVALID_ADDRESS;
      m_temporary_allocation_size = 0;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    const lldb::addr_t load_addr = process_address + m_offset;
    dump_stream.Printf("0x%" PRIx64 ": EntityVariable\n", load_addr);

    Status err;

    lldb::addr_t ptr = LLDB_INVALID_ADDRESS;

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                map.GetByteOrder(), map.GetAddressByteSize());

        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        lldb::offset_t offset;

        ptr = extractor.GetPointer(&offset);

        dump_stream.PutChar('\n');
      }
    }

    if (m_temporary_allocation == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("Points to process memory:\n");
    } else {
      dump_stream.Printf("Temporary allocation:\n");
    }

    if (ptr == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("  <could not be be found>\n");
    } else {
      DataBufferHeap data(m_temporary_allocation_size, 0);

      map.ReadMemory(data.GetBytes(), m_temporary_allocation,
                     m_temporary_allocation_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {
    if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      Status free_error;

      map.Free(m_temporary_allocation, free_error);

      m_temporary_allocation = LLDB_INVALID_ADDRESS;
      m_temporary_allocation_size = 0;
    }
  }

private:
  lldb::VariableSP m_variable_sp;
  bool m_is_reference;
  lldb::addr_t m_temporary_allocation;
  size_t m_temporary_allocation_size;
  lldb::DataBufferSP m_original_data;
};

uint32_t Materializer::AddVariable(lldb::VariableSP &variable_sp, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  iter->reset(new EntityVariable(variable_sp));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntityResultVariable : public Materializer::Entity {
public:
  EntityResultVariable(const CompilerType &type, bool is_program_reference,
                       bool keep_in_memory,
                       Materializer::PersistentVariableDelegate *delegate)
      : Entity(), m_type(type), m_is_program_reference(is_program_reference),
        m_keep_in_memory(keep_in_memory),
        m_temporary_allocation(LLDB_INVALID_ADDRESS),
        m_temporary_allocation_size(0), m_delegate(delegate) {
    // Hard-coding to maximum size of a pointer since all results are
    // materialized by reference
    m_size = 8;
    m_alignment = 8;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    if (!m_is_program_reference) {
      if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
        err.SetErrorString("Trying to create a temporary region for the result "
                           "but one exists");
        return;
      }

      const lldb::addr_t load_addr = process_address + m_offset;

      ExecutionContextScope *exe_scope = map.GetBestExecutionContextScope();

      llvm::Optional<uint64_t> byte_size = m_type.GetByteSize(exe_scope);
      if (!byte_size) {
        err.SetErrorString("can't get size of type");
        return;
      }
      size_t bit_align = m_type.GetTypeBitAlign();
      size_t byte_align = (bit_align + 7) / 8;

      if (!byte_align)
        byte_align = 1;

      Status alloc_error;
      const bool zero_memory = true;

      m_temporary_allocation = map.Malloc(
          *byte_size, byte_align,
          lldb::ePermissionsReadable | lldb::ePermissionsWritable,
          IRMemoryMap::eAllocationPolicyMirror, zero_memory, alloc_error);
      m_temporary_allocation_size = *byte_size;

      if (!alloc_error.Success()) {
        err.SetErrorStringWithFormat(
            "couldn't allocate a temporary region for the result: %s",
            alloc_error.AsCString());
        return;
      }

      Status pointer_write_error;

      map.WritePointerToMemory(load_addr, m_temporary_allocation,
                               pointer_write_error);

      if (!pointer_write_error.Success()) {
        err.SetErrorStringWithFormat("couldn't write the address of the "
                                     "temporary region for the result: %s",
                                     pointer_write_error.AsCString());
      }
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    err.Clear();

    ExecutionContextScope *exe_scope = map.GetBestExecutionContextScope();

    if (!exe_scope) {
      err.SetErrorString("Couldn't dematerialize a result variable: invalid "
                         "execution context scope");
      return;
    }

    lldb::addr_t address;
    Status read_error;
    const lldb::addr_t load_addr = process_address + m_offset;

    map.ReadPointerFromMemory(&address, load_addr, read_error);

    if (!read_error.Success()) {
      err.SetErrorString("Couldn't dematerialize a result variable: couldn't "
                         "read its address");
      return;
    }

    lldb::TargetSP target_sp = exe_scope->CalculateTarget();

    if (!target_sp) {
      err.SetErrorString("Couldn't dematerialize a result variable: no target");
      return;
    }

    Status type_system_error;
    TypeSystem *type_system = target_sp->GetScratchTypeSystemForLanguage(
        &type_system_error, m_type.GetMinimumLanguage());

    if (!type_system) {
      err.SetErrorStringWithFormat("Couldn't dematerialize a result variable: "
                                   "couldn't get the corresponding type "
                                   "system: %s",
                                   type_system_error.AsCString());
      return;
    }

    PersistentExpressionState *persistent_state =
        type_system->GetPersistentExpressionState();

    if (!persistent_state) {
      err.SetErrorString("Couldn't dematerialize a result variable: "
                         "corresponding type system doesn't handle persistent "
                         "variables");
      return;
    }

    ConstString name =
        m_delegate
            ? m_delegate->GetName()
            : persistent_state->GetNextPersistentVariableName(
                  *target_sp, persistent_state->GetPersistentVariablePrefix());

    lldb::ExpressionVariableSP ret = persistent_state->CreatePersistentVariable(
        exe_scope, name, m_type, map.GetByteOrder(), map.GetAddressByteSize());

    if (!ret) {
      err.SetErrorStringWithFormat("couldn't dematerialize a result variable: "
                                   "failed to make persistent variable %s",
                                   name.AsCString());
      return;
    }

    lldb::ProcessSP process_sp =
        map.GetBestExecutionContextScope()->CalculateProcess();

    if (m_delegate) {
      m_delegate->DidDematerialize(ret);
    }

    bool can_persist =
        (m_is_program_reference && process_sp && process_sp->CanJIT() &&
         !(address >= frame_bottom && address < frame_top));

    if (can_persist && m_keep_in_memory) {
      ret->m_live_sp = ValueObjectConstResult::Create(exe_scope, m_type, name,
                                                      address, eAddressTypeLoad,
                                                      map.GetAddressByteSize());
    }

    ret->ValueUpdated();

    const size_t pvar_byte_size = ret->GetByteSize();
    uint8_t *pvar_data = ret->GetValueBytes();

    map.ReadMemory(pvar_data, address, pvar_byte_size, read_error);

    if (!read_error.Success()) {
      err.SetErrorString(
          "Couldn't dematerialize a result variable: couldn't read its memory");
      return;
    }

    if (!can_persist || !m_keep_in_memory) {
      ret->m_flags |= ExpressionVariable::EVNeedsAllocation;

      if (m_temporary_allocation != LLDB_INVALID_ADDRESS) {
        Status free_error;
        map.Free(m_temporary_allocation, free_error);
      }
    } else {
      ret->m_flags |= ExpressionVariable::EVIsLLDBAllocated;
    }

    m_temporary_allocation = LLDB_INVALID_ADDRESS;
    m_temporary_allocation_size = 0;
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityResultVariable\n", load_addr);

    Status err;

    lldb::addr_t ptr = LLDB_INVALID_ADDRESS;

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DataExtractor extractor(data.GetBytes(), data.GetByteSize(),
                                map.GetByteOrder(), map.GetAddressByteSize());

        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        lldb::offset_t offset;

        ptr = extractor.GetPointer(&offset);

        dump_stream.PutChar('\n');
      }
    }

    if (m_temporary_allocation == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("Points to process memory:\n");
    } else {
      dump_stream.Printf("Temporary allocation:\n");
    }

    if (ptr == LLDB_INVALID_ADDRESS) {
      dump_stream.Printf("  <could not be be found>\n");
    } else {
      DataBufferHeap data(m_temporary_allocation_size, 0);

      map.ReadMemory(data.GetBytes(), m_temporary_allocation,
                     m_temporary_allocation_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {
    if (!m_keep_in_memory && m_temporary_allocation != LLDB_INVALID_ADDRESS) {
      Status free_error;

      map.Free(m_temporary_allocation, free_error);
    }

    m_temporary_allocation = LLDB_INVALID_ADDRESS;
    m_temporary_allocation_size = 0;
  }

private:
  CompilerType m_type;
  bool m_is_program_reference;
  bool m_keep_in_memory;

  lldb::addr_t m_temporary_allocation;
  size_t m_temporary_allocation_size;
  Materializer::PersistentVariableDelegate *m_delegate;
};

uint32_t Materializer::AddResultVariable(const CompilerType &type,
                                         bool is_program_reference,
                                         bool keep_in_memory,
                                         PersistentVariableDelegate *delegate,
                                         Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  iter->reset(new EntityResultVariable(type, is_program_reference,
                                       keep_in_memory, delegate));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntitySymbol : public Materializer::Entity {
public:
  EntitySymbol(const Symbol &symbol) : Entity(), m_symbol(symbol) {
    // Hard-coding to maximum size of a symbol
    m_size = 8;
    m_alignment = 8;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf("EntitySymbol::Materialize [address = 0x%" PRIx64
                  ", m_symbol = %s]",
                  (uint64_t)load_addr, m_symbol.GetName().AsCString());
    }

    const Address sym_address = m_symbol.GetAddress();

    ExecutionContextScope *exe_scope = map.GetBestExecutionContextScope();

    lldb::TargetSP target_sp;

    if (exe_scope)
      target_sp = map.GetBestExecutionContextScope()->CalculateTarget();

    if (!target_sp) {
      err.SetErrorStringWithFormat(
          "couldn't resolve symbol %s because there is no target",
          m_symbol.GetName().AsCString());
      return;
    }

    lldb::addr_t resolved_address = sym_address.GetLoadAddress(target_sp.get());

    if (resolved_address == LLDB_INVALID_ADDRESS)
      resolved_address = sym_address.GetFileAddress();

    Status pointer_write_error;

    map.WritePointerToMemory(load_addr, resolved_address, pointer_write_error);

    if (!pointer_write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write the address of symbol %s: %s",
          m_symbol.GetName().AsCString(), pointer_write_error.AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf("EntitySymbol::Dematerialize [address = 0x%" PRIx64
                  ", m_symbol = %s]",
                  (uint64_t)load_addr, m_symbol.GetName().AsCString());
    }

    // no work needs to be done
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntitySymbol (%s)\n", load_addr,
                       m_symbol.GetName().AsCString());

    {
      dump_stream.Printf("Pointer:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  Symbol m_symbol;
};

uint32_t Materializer::AddSymbol(const Symbol &symbol_sp, Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  iter->reset(new EntitySymbol(symbol_sp));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

class EntityRegister : public Materializer::Entity {
public:
  EntityRegister(const RegisterInfo &register_info)
      : Entity(), m_register_info(register_info) {
    // Hard-coding alignment conservatively
    m_size = m_register_info.byte_size;
    m_alignment = m_register_info.byte_size;
  }

  void Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                   lldb::addr_t process_address, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf("EntityRegister::Materialize [address = 0x%" PRIx64
                  ", m_register_info = %s]",
                  (uint64_t)load_addr, m_register_info.name);
    }

    RegisterValue reg_value;

    if (!frame_sp.get()) {
      err.SetErrorStringWithFormat(
          "couldn't materialize register %s without a stack frame",
          m_register_info.name);
      return;
    }

    lldb::RegisterContextSP reg_context_sp = frame_sp->GetRegisterContext();

    if (!reg_context_sp->ReadRegister(&m_register_info, reg_value)) {
      err.SetErrorStringWithFormat("couldn't read the value of register %s",
                                   m_register_info.name);
      return;
    }

    DataExtractor register_data;

    if (!reg_value.GetData(register_data)) {
      err.SetErrorStringWithFormat("couldn't get the data for register %s",
                                   m_register_info.name);
      return;
    }

    if (register_data.GetByteSize() != m_register_info.byte_size) {
      err.SetErrorStringWithFormat(
          "data for register %s had size %llu but we expected %llu",
          m_register_info.name, (unsigned long long)register_data.GetByteSize(),
          (unsigned long long)m_register_info.byte_size);
      return;
    }

    m_register_contents.reset(new DataBufferHeap(register_data.GetDataStart(),
                                                 register_data.GetByteSize()));

    Status write_error;

    map.WriteMemory(load_addr, register_data.GetDataStart(),
                    register_data.GetByteSize(), write_error);

    if (!write_error.Success()) {
      err.SetErrorStringWithFormat(
          "couldn't write the contents of register %s: %s",
          m_register_info.name, write_error.AsCString());
      return;
    }
  }

  void Dematerialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                     lldb::addr_t process_address, lldb::addr_t frame_top,
                     lldb::addr_t frame_bottom, Status &err) override {
    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

    const lldb::addr_t load_addr = process_address + m_offset;

    if (log) {
      log->Printf("EntityRegister::Dematerialize [address = 0x%" PRIx64
                  ", m_register_info = %s]",
                  (uint64_t)load_addr, m_register_info.name);
    }

    Status extract_error;

    DataExtractor register_data;

    if (!frame_sp.get()) {
      err.SetErrorStringWithFormat(
          "couldn't dematerialize register %s without a stack frame",
          m_register_info.name);
      return;
    }

    lldb::RegisterContextSP reg_context_sp = frame_sp->GetRegisterContext();

    map.GetMemoryData(register_data, load_addr, m_register_info.byte_size,
                      extract_error);

    if (!extract_error.Success()) {
      err.SetErrorStringWithFormat("couldn't get the data for register %s: %s",
                                   m_register_info.name,
                                   extract_error.AsCString());
      return;
    }

    if (!memcmp(register_data.GetDataStart(), m_register_contents->GetBytes(),
                register_data.GetByteSize())) {
      // No write required, and in particular we avoid errors if the register
      // wasn't writable

      m_register_contents.reset();
      return;
    }

    m_register_contents.reset();

    RegisterValue register_value(
        const_cast<uint8_t *>(register_data.GetDataStart()),
        register_data.GetByteSize(), register_data.GetByteOrder());

    if (!reg_context_sp->WriteRegister(&m_register_info, register_value)) {
      err.SetErrorStringWithFormat("couldn't write the value of register %s",
                                   m_register_info.name);
      return;
    }
  }

  void DumpToLog(IRMemoryMap &map, lldb::addr_t process_address,
                 Log *log) override {
    StreamString dump_stream;

    Status err;

    const lldb::addr_t load_addr = process_address + m_offset;

    dump_stream.Printf("0x%" PRIx64 ": EntityRegister (%s)\n", load_addr,
                       m_register_info.name);

    {
      dump_stream.Printf("Value:\n");

      DataBufferHeap data(m_size, 0);

      map.ReadMemory(data.GetBytes(), load_addr, m_size, err);

      if (!err.Success()) {
        dump_stream.Printf("  <could not be read>\n");
      } else {
        DumpHexBytes(&dump_stream, data.GetBytes(), data.GetByteSize(), 16,
                     load_addr);

        dump_stream.PutChar('\n');
      }
    }

    log->PutString(dump_stream.GetString());
  }

  void Wipe(IRMemoryMap &map, lldb::addr_t process_address) override {}

private:
  RegisterInfo m_register_info;
  lldb::DataBufferSP m_register_contents;
};

uint32_t Materializer::AddRegister(const RegisterInfo &register_info,
                                   Status &err) {
  EntityVector::iterator iter = m_entities.insert(m_entities.end(), EntityUP());
  iter->reset(new EntityRegister(register_info));
  uint32_t ret = AddStructMember(**iter);
  (*iter)->SetOffset(ret);
  return ret;
}

Materializer::Materializer()
    : m_dematerializer_wp(), m_current_offset(0), m_struct_alignment(8) {}

Materializer::~Materializer() {
  DematerializerSP dematerializer_sp = m_dematerializer_wp.lock();

  if (dematerializer_sp)
    dematerializer_sp->Wipe();
}

Materializer::DematerializerSP
Materializer::Materialize(lldb::StackFrameSP &frame_sp, IRMemoryMap &map,
                          lldb::addr_t process_address, Status &error) {
  ExecutionContextScope *exe_scope = frame_sp.get();

  if (!exe_scope)
    exe_scope = map.GetBestExecutionContextScope();

  DematerializerSP dematerializer_sp = m_dematerializer_wp.lock();

  if (dematerializer_sp) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't materialize: already materialized");
  }

  DematerializerSP ret(
      new Dematerializer(*this, frame_sp, map, process_address));

  if (!exe_scope) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't materialize: target doesn't exist");
  }

  for (EntityUP &entity_up : m_entities) {
    entity_up->Materialize(frame_sp, map, process_address, error);

    if (!error.Success())
      return DematerializerSP();
  }

  if (Log *log =
          lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS)) {
    log->Printf(
        "Materializer::Materialize (frame_sp = %p, process_address = 0x%" PRIx64
        ") materialized:",
        static_cast<void *>(frame_sp.get()), process_address);
    for (EntityUP &entity_up : m_entities)
      entity_up->DumpToLog(map, process_address, log);
  }

  m_dematerializer_wp = ret;

  return ret;
}

void Materializer::Dematerializer::Dematerialize(Status &error,
                                                 lldb::addr_t frame_bottom,
                                                 lldb::addr_t frame_top) {
  lldb::StackFrameSP frame_sp;

  lldb::ThreadSP thread_sp = m_thread_wp.lock();
  if (thread_sp)
    frame_sp = thread_sp->GetFrameWithStackID(m_stack_id);

  ExecutionContextScope *exe_scope = m_map->GetBestExecutionContextScope();

  if (!IsValid()) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't dematerialize: invalid dematerializer");
  }

  if (!exe_scope) {
    error.SetErrorToGenericError();
    error.SetErrorString("Couldn't dematerialize: target is gone");
  } else {
    if (Log *log =
            lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS)) {
      log->Printf("Materializer::Dematerialize (frame_sp = %p, process_address "
                  "= 0x%" PRIx64 ") about to dematerialize:",
                  static_cast<void *>(frame_sp.get()), m_process_address);
      for (EntityUP &entity_up : m_materializer->m_entities)
        entity_up->DumpToLog(*m_map, m_process_address, log);
    }

    for (EntityUP &entity_up : m_materializer->m_entities) {
      entity_up->Dematerialize(frame_sp, *m_map, m_process_address, frame_top,
                               frame_bottom, error);

      if (!error.Success())
        break;
    }
  }

  Wipe();
}

void Materializer::Dematerializer::Wipe() {
  if (!IsValid())
    return;

  for (EntityUP &entity_up : m_materializer->m_entities) {
    entity_up->Wipe(*m_map, m_process_address);
  }

  m_materializer = nullptr;
  m_map = nullptr;
  m_process_address = LLDB_INVALID_ADDRESS;
}

Materializer::PersistentVariableDelegate::~PersistentVariableDelegate() =
    default;

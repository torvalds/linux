//===-- AppleObjCClassDescriptorV2.cpp -----------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AppleObjCClassDescriptorV2.h"

#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

bool ClassDescriptorV2::Read_objc_class(
    Process *process, std::unique_ptr<objc_class_t> &objc_class) const {
  objc_class.reset(new objc_class_t);

  bool ret = objc_class->Read(process, m_objc_class_ptr);

  if (!ret)
    objc_class.reset();

  return ret;
}

static lldb::addr_t GetClassDataMask(Process *process) {
  switch (process->GetAddressByteSize()) {
  case 4:
    return 0xfffffffcUL;
  case 8:
    return 0x00007ffffffffff8UL;
  default:
    break;
  }

  return LLDB_INVALID_ADDRESS;
}

bool ClassDescriptorV2::objc_class_t::Read(Process *process,
                                           lldb::addr_t addr) {
  size_t ptr_size = process->GetAddressByteSize();

  size_t objc_class_size = ptr_size    // uintptr_t isa;
                           + ptr_size  // Class superclass;
                           + ptr_size  // void *cache;
                           + ptr_size  // IMP *vtable;
                           + ptr_size; // uintptr_t data_NEVER_USE;

  DataBufferHeap objc_class_buf(objc_class_size, '\0');
  Status error;

  process->ReadMemory(addr, objc_class_buf.GetBytes(), objc_class_size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(objc_class_buf.GetBytes(), objc_class_size,
                          process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_isa = extractor.GetAddress_unchecked(&cursor);        // uintptr_t isa;
  m_superclass = extractor.GetAddress_unchecked(&cursor); // Class superclass;
  m_cache_ptr = extractor.GetAddress_unchecked(&cursor);  // void *cache;
  m_vtable_ptr = extractor.GetAddress_unchecked(&cursor); // IMP *vtable;
  lldb::addr_t data_NEVER_USE =
      extractor.GetAddress_unchecked(&cursor); // uintptr_t data_NEVER_USE;

  m_flags = (uint8_t)(data_NEVER_USE & (lldb::addr_t)3);
  m_data_ptr = data_NEVER_USE & GetClassDataMask(process);

  return true;
}

bool ClassDescriptorV2::class_rw_t::Read(Process *process, lldb::addr_t addr) {
  size_t ptr_size = process->GetAddressByteSize();

  size_t size = sizeof(uint32_t)   // uint32_t flags;
                + sizeof(uint32_t) // uint32_t version;
                + ptr_size         // const class_ro_t *ro;
                + ptr_size         // union { method_list_t **method_lists;
                                   // method_list_t *method_list; };
                + ptr_size         // struct chained_property_list *properties;
                + ptr_size         // const protocol_list_t **protocols;
                + ptr_size         // Class firstSubclass;
                + ptr_size;        // Class nextSiblingClass;

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_flags = extractor.GetU32_unchecked(&cursor);
  m_version = extractor.GetU32_unchecked(&cursor);
  m_ro_ptr = extractor.GetAddress_unchecked(&cursor);
  m_method_list_ptr = extractor.GetAddress_unchecked(&cursor);
  m_properties_ptr = extractor.GetAddress_unchecked(&cursor);
  m_firstSubclass = extractor.GetAddress_unchecked(&cursor);
  m_nextSiblingClass = extractor.GetAddress_unchecked(&cursor);

  return true;
}

bool ClassDescriptorV2::class_ro_t::Read(Process *process, lldb::addr_t addr) {
  size_t ptr_size = process->GetAddressByteSize();

  size_t size = sizeof(uint32_t)   // uint32_t flags;
                + sizeof(uint32_t) // uint32_t instanceStart;
                + sizeof(uint32_t) // uint32_t instanceSize;
                + (ptr_size == 8 ? sizeof(uint32_t)
                                 : 0) // uint32_t reserved; // __LP64__ only
                + ptr_size            // const uint8_t *ivarLayout;
                + ptr_size            // const char *name;
                + ptr_size            // const method_list_t *baseMethods;
                + ptr_size            // const protocol_list_t *baseProtocols;
                + ptr_size            // const ivar_list_t *ivars;
                + ptr_size            // const uint8_t *weakIvarLayout;
                + ptr_size;           // const property_list_t *baseProperties;

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_flags = extractor.GetU32_unchecked(&cursor);
  m_instanceStart = extractor.GetU32_unchecked(&cursor);
  m_instanceSize = extractor.GetU32_unchecked(&cursor);
  if (ptr_size == 8)
    m_reserved = extractor.GetU32_unchecked(&cursor);
  else
    m_reserved = 0;
  m_ivarLayout_ptr = extractor.GetAddress_unchecked(&cursor);
  m_name_ptr = extractor.GetAddress_unchecked(&cursor);
  m_baseMethods_ptr = extractor.GetAddress_unchecked(&cursor);
  m_baseProtocols_ptr = extractor.GetAddress_unchecked(&cursor);
  m_ivars_ptr = extractor.GetAddress_unchecked(&cursor);
  m_weakIvarLayout_ptr = extractor.GetAddress_unchecked(&cursor);
  m_baseProperties_ptr = extractor.GetAddress_unchecked(&cursor);

  DataBufferHeap name_buf(1024, '\0');

  process->ReadCStringFromMemory(m_name_ptr, (char *)name_buf.GetBytes(),
                                 name_buf.GetByteSize(), error);

  if (error.Fail()) {
    return false;
  }

  m_name.assign((char *)name_buf.GetBytes());

  return true;
}

bool ClassDescriptorV2::Read_class_row(
    Process *process, const objc_class_t &objc_class,
    std::unique_ptr<class_ro_t> &class_ro,
    std::unique_ptr<class_rw_t> &class_rw) const {
  class_ro.reset();
  class_rw.reset();

  Status error;
  uint32_t class_row_t_flags = process->ReadUnsignedIntegerFromMemory(
      objc_class.m_data_ptr, sizeof(uint32_t), 0, error);
  if (!error.Success())
    return false;

  if (class_row_t_flags & RW_REALIZED) {
    class_rw.reset(new class_rw_t);

    if (!class_rw->Read(process, objc_class.m_data_ptr)) {
      class_rw.reset();
      return false;
    }

    class_ro.reset(new class_ro_t);

    if (!class_ro->Read(process, class_rw->m_ro_ptr)) {
      class_rw.reset();
      class_ro.reset();
      return false;
    }
  } else {
    class_ro.reset(new class_ro_t);

    if (!class_ro->Read(process, objc_class.m_data_ptr)) {
      class_ro.reset();
      return false;
    }
  }

  return true;
}

bool ClassDescriptorV2::method_list_t::Read(Process *process,
                                            lldb::addr_t addr) {
  size_t size = sizeof(uint32_t)    // uint32_t entsize_NEVER_USE;
                + sizeof(uint32_t); // uint32_t count;

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_entsize = extractor.GetU32_unchecked(&cursor) & ~(uint32_t)3;
  m_count = extractor.GetU32_unchecked(&cursor);
  m_first_ptr = addr + cursor;

  return true;
}

bool ClassDescriptorV2::method_t::Read(Process *process, lldb::addr_t addr) {
  size_t size = GetSize(process);

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_name_ptr = extractor.GetAddress_unchecked(&cursor);
  m_types_ptr = extractor.GetAddress_unchecked(&cursor);
  m_imp_ptr = extractor.GetAddress_unchecked(&cursor);

  process->ReadCStringFromMemory(m_name_ptr, m_name, error);
  if (error.Fail()) {
    return false;
  }

  process->ReadCStringFromMemory(m_types_ptr, m_types, error);
  return !error.Fail();
}

bool ClassDescriptorV2::ivar_list_t::Read(Process *process, lldb::addr_t addr) {
  size_t size = sizeof(uint32_t)    // uint32_t entsize;
                + sizeof(uint32_t); // uint32_t count;

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_entsize = extractor.GetU32_unchecked(&cursor);
  m_count = extractor.GetU32_unchecked(&cursor);
  m_first_ptr = addr + cursor;

  return true;
}

bool ClassDescriptorV2::ivar_t::Read(Process *process, lldb::addr_t addr) {
  size_t size = GetSize(process);

  DataBufferHeap buffer(size, '\0');
  Status error;

  process->ReadMemory(addr, buffer.GetBytes(), size, error);
  if (error.Fail()) {
    return false;
  }

  DataExtractor extractor(buffer.GetBytes(), size, process->GetByteOrder(),
                          process->GetAddressByteSize());

  lldb::offset_t cursor = 0;

  m_offset_ptr = extractor.GetAddress_unchecked(&cursor);
  m_name_ptr = extractor.GetAddress_unchecked(&cursor);
  m_type_ptr = extractor.GetAddress_unchecked(&cursor);
  m_alignment = extractor.GetU32_unchecked(&cursor);
  m_size = extractor.GetU32_unchecked(&cursor);

  process->ReadCStringFromMemory(m_name_ptr, m_name, error);
  if (error.Fail()) {
    return false;
  }

  process->ReadCStringFromMemory(m_type_ptr, m_type, error);
  return !error.Fail();
}

bool ClassDescriptorV2::Describe(
    std::function<void(ObjCLanguageRuntime::ObjCISA)> const &superclass_func,
    std::function<bool(const char *, const char *)> const &instance_method_func,
    std::function<bool(const char *, const char *)> const &class_method_func,
    std::function<bool(const char *, const char *, lldb::addr_t,
                       uint64_t)> const &ivar_func) const {
  lldb_private::Process *process = m_runtime.GetProcess();

  std::unique_ptr<objc_class_t> objc_class;
  std::unique_ptr<class_ro_t> class_ro;
  std::unique_ptr<class_rw_t> class_rw;

  if (!Read_objc_class(process, objc_class))
    return 0;
  if (!Read_class_row(process, *objc_class, class_ro, class_rw))
    return 0;

  static ConstString NSObject_name("NSObject");

  if (m_name != NSObject_name && superclass_func)
    superclass_func(objc_class->m_superclass);

  if (instance_method_func) {
    std::unique_ptr<method_list_t> base_method_list;

    base_method_list.reset(new method_list_t);
    if (!base_method_list->Read(process, class_ro->m_baseMethods_ptr))
      return false;

    if (base_method_list->m_entsize != method_t::GetSize(process))
      return false;

    std::unique_ptr<method_t> method;
    method.reset(new method_t);

    for (uint32_t i = 0, e = base_method_list->m_count; i < e; ++i) {
      method->Read(process, base_method_list->m_first_ptr +
                                (i * base_method_list->m_entsize));

      if (instance_method_func(method->m_name.c_str(), method->m_types.c_str()))
        break;
    }
  }

  if (class_method_func) {
    AppleObjCRuntime::ClassDescriptorSP metaclass(GetMetaclass());

    // We don't care about the metaclass's superclass, or its class methods.
    // Its instance methods are our class methods.

    if (metaclass) {
      metaclass->Describe(
          std::function<void(ObjCLanguageRuntime::ObjCISA)>(nullptr),
          class_method_func,
          std::function<bool(const char *, const char *)>(nullptr),
          std::function<bool(const char *, const char *, lldb::addr_t,
                             uint64_t)>(nullptr));
    }
  }

  if (ivar_func) {
    if (class_ro->m_ivars_ptr != 0) {
      ivar_list_t ivar_list;
      if (!ivar_list.Read(process, class_ro->m_ivars_ptr))
        return false;

      if (ivar_list.m_entsize != ivar_t::GetSize(process))
        return false;

      ivar_t ivar;

      for (uint32_t i = 0, e = ivar_list.m_count; i < e; ++i) {
        ivar.Read(process, ivar_list.m_first_ptr + (i * ivar_list.m_entsize));

        if (ivar_func(ivar.m_name.c_str(), ivar.m_type.c_str(),
                      ivar.m_offset_ptr, ivar.m_size))
          break;
      }
    }
  }

  return true;
}

ConstString ClassDescriptorV2::GetClassName() {
  if (!m_name) {
    lldb_private::Process *process = m_runtime.GetProcess();

    if (process) {
      std::unique_ptr<objc_class_t> objc_class;
      std::unique_ptr<class_ro_t> class_ro;
      std::unique_ptr<class_rw_t> class_rw;

      if (!Read_objc_class(process, objc_class))
        return m_name;
      if (!Read_class_row(process, *objc_class, class_ro, class_rw))
        return m_name;

      m_name = ConstString(class_ro->m_name.c_str());
    }
  }
  return m_name;
}

ObjCLanguageRuntime::ClassDescriptorSP ClassDescriptorV2::GetSuperclass() {
  lldb_private::Process *process = m_runtime.GetProcess();

  if (!process)
    return ObjCLanguageRuntime::ClassDescriptorSP();

  std::unique_ptr<objc_class_t> objc_class;

  if (!Read_objc_class(process, objc_class))
    return ObjCLanguageRuntime::ClassDescriptorSP();

  return m_runtime.ObjCLanguageRuntime::GetClassDescriptorFromISA(
      objc_class->m_superclass);
}

ObjCLanguageRuntime::ClassDescriptorSP ClassDescriptorV2::GetMetaclass() const {
  lldb_private::Process *process = m_runtime.GetProcess();

  if (!process)
    return ObjCLanguageRuntime::ClassDescriptorSP();

  std::unique_ptr<objc_class_t> objc_class;

  if (!Read_objc_class(process, objc_class))
    return ObjCLanguageRuntime::ClassDescriptorSP();

  lldb::addr_t candidate_isa = m_runtime.GetPointerISA(objc_class->m_isa);

  return ObjCLanguageRuntime::ClassDescriptorSP(
      new ClassDescriptorV2(m_runtime, candidate_isa, nullptr));
}

uint64_t ClassDescriptorV2::GetInstanceSize() {
  lldb_private::Process *process = m_runtime.GetProcess();

  if (process) {
    std::unique_ptr<objc_class_t> objc_class;
    std::unique_ptr<class_ro_t> class_ro;
    std::unique_ptr<class_rw_t> class_rw;

    if (!Read_objc_class(process, objc_class))
      return 0;
    if (!Read_class_row(process, *objc_class, class_ro, class_rw))
      return 0;

    return class_ro->m_instanceSize;
  }

  return 0;
}

ClassDescriptorV2::iVarsStorage::iVarsStorage()
    : m_filled(false), m_ivars(), m_mutex() {}

size_t ClassDescriptorV2::iVarsStorage::size() { return m_ivars.size(); }

ClassDescriptorV2::iVarDescriptor &ClassDescriptorV2::iVarsStorage::
operator[](size_t idx) {
  return m_ivars[idx];
}

void ClassDescriptorV2::iVarsStorage::fill(AppleObjCRuntimeV2 &runtime,
                                           ClassDescriptorV2 &descriptor) {
  if (m_filled)
    return;
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_TYPES));
  LLDB_LOGV(log, "class_name = {0}", descriptor.GetClassName());
  m_filled = true;
  ObjCLanguageRuntime::EncodingToTypeSP encoding_to_type_sp(
      runtime.GetEncodingToType());
  Process *process(runtime.GetProcess());
  if (!encoding_to_type_sp)
    return;
  descriptor.Describe(nullptr, nullptr, nullptr, [this, process,
                                                  encoding_to_type_sp,
                                                  log](const char *name,
                                                       const char *type,
                                                       lldb::addr_t offset_ptr,
                                                       uint64_t size) -> bool {
    const bool for_expression = false;
    const bool stop_loop = false;
    LLDB_LOGV(log, "name = {0}, encoding = {1}, offset_ptr = {2:x}, size = {3}",
              name, type, offset_ptr, size);
    CompilerType ivar_type =
        encoding_to_type_sp->RealizeType(type, for_expression);
    if (ivar_type) {
      LLDB_LOGV(log,
                "name = {0}, encoding = {1}, offset_ptr = {2:x}, size = "
                "{3}, type_size = {4}",
                name, type, offset_ptr, size,
                ivar_type.GetByteSize(nullptr).getValueOr(0));
      Scalar offset_scalar;
      Status error;
      const int offset_ptr_size = 4;
      const bool is_signed = false;
      size_t read = process->ReadScalarIntegerFromMemory(
          offset_ptr, offset_ptr_size, is_signed, offset_scalar, error);
      if (error.Success() && 4 == read) {
        LLDB_LOGV(log, "offset_ptr = {0:x} --> {1}", offset_ptr,
                  offset_scalar.SInt());
        m_ivars.push_back(
            {ConstString(name), ivar_type, size, offset_scalar.SInt()});
      } else
        LLDB_LOGV(log, "offset_ptr = {0:x} --> read fail, read = %{1}",
                  offset_ptr, read);
    }
    return stop_loop;
  });
}

void ClassDescriptorV2::GetIVarInformation() {
  m_ivars_storage.fill(m_runtime, *this);
}

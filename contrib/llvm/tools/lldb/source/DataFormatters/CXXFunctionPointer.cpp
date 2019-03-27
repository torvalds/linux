//===-- CXXFunctionPointer.cpp-----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/CXXFunctionPointer.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"

#include <string>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

bool lldb_private::formatters::CXXFunctionPointerSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  std::string destination;
  StreamString sstr;
  AddressType func_ptr_address_type = eAddressTypeInvalid;
  addr_t func_ptr_address = valobj.GetPointerValue(&func_ptr_address_type);
  if (func_ptr_address != 0 && func_ptr_address != LLDB_INVALID_ADDRESS) {
    switch (func_ptr_address_type) {
    case eAddressTypeInvalid:
    case eAddressTypeFile:
    case eAddressTypeHost:
      break;

    case eAddressTypeLoad: {
      ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

      Address so_addr;
      Target *target = exe_ctx.GetTargetPtr();
      if (target && !target->GetSectionLoadList().IsEmpty()) {
        if (target->GetSectionLoadList().ResolveLoadAddress(func_ptr_address,
                                                            so_addr)) {
          so_addr.Dump(&sstr, exe_ctx.GetBestExecutionContextScope(),
                       Address::DumpStyleResolvedDescription,
                       Address::DumpStyleSectionNameOffset);
        }
      }
    } break;
    }
  }
  if (sstr.GetSize() > 0) {
    stream.Printf("(%s)", sstr.GetData());
    return true;
  } else
    return false;
}

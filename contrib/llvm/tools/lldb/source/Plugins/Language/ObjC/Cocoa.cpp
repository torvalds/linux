//===-- Cocoa.cpp -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Cocoa.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/StringPrinter.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/Host/Time.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/ProcessStructReader.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/APInt.h"

#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntime.h"

#include "NSString.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

bool lldb_private::formatters::NSBundleSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  if (class_name == "NSBundle") {
    uint64_t offset = 5 * ptr_size;
    ValueObjectSP text(valobj.GetSyntheticChildAtOffset(
        offset,
        valobj.GetCompilerType().GetBasicTypeFromAST(lldb::eBasicTypeObjCID),
        true));

    StreamString summary_stream;
    bool was_nsstring_ok =
        NSStringSummaryProvider(*text, summary_stream, options);
    if (was_nsstring_ok && summary_stream.GetSize() > 0) {
      stream.Printf("%s", summary_stream.GetData());
      return true;
    }
  }

  return false;
}

bool lldb_private::formatters::NSTimeZoneSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  if (class_name == "__NSTimeZone") {
    uint64_t offset = ptr_size;
    ValueObjectSP text(valobj.GetSyntheticChildAtOffset(
        offset, valobj.GetCompilerType(), true));
    StreamString summary_stream;
    bool was_nsstring_ok =
        NSStringSummaryProvider(*text, summary_stream, options);
    if (was_nsstring_ok && summary_stream.GetSize() > 0) {
      stream.Printf("%s", summary_stream.GetData());
      return true;
    }
  }

  return false;
}

bool lldb_private::formatters::NSNotificationSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  if (class_name == "NSConcreteNotification") {
    uint64_t offset = ptr_size;
    ValueObjectSP text(valobj.GetSyntheticChildAtOffset(
        offset, valobj.GetCompilerType(), true));
    StreamString summary_stream;
    bool was_nsstring_ok =
        NSStringSummaryProvider(*text, summary_stream, options);
    if (was_nsstring_ok && summary_stream.GetSize() > 0) {
      stream.Printf("%s", summary_stream.GetData());
      return true;
    }
  }

  return false;
}

bool lldb_private::formatters::NSMachPortSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  uint64_t port_number = 0;

  if (class_name == "NSMachPort") {
    uint64_t offset = (ptr_size == 4 ? 12 : 20);
    Status error;
    port_number = process_sp->ReadUnsignedIntegerFromMemory(
        offset + valobj_addr, 4, 0, error);
    if (error.Success()) {
      stream.Printf("mach port: %u",
                    (uint32_t)(port_number & 0x00000000FFFFFFFF));
      return true;
    }
  }

  return false;
}

bool lldb_private::formatters::NSIndexSetSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  uint64_t count = 0;

  do {
    if (class_name == "NSIndexSet" || class_name == "NSMutableIndexSet") {
      Status error;
      uint32_t mode = process_sp->ReadUnsignedIntegerFromMemory(
          valobj_addr + ptr_size, 4, 0, error);
      if (error.Fail())
        return false;
      // this means the set is empty - count = 0
      if ((mode & 1) == 1) {
        count = 0;
        break;
      }
      if ((mode & 2) == 2)
        mode = 1; // this means the set only has one range
      else
        mode = 2; // this means the set has multiple ranges
      if (mode == 1) {
        count = process_sp->ReadUnsignedIntegerFromMemory(
            valobj_addr + 3 * ptr_size, ptr_size, 0, error);
        if (error.Fail())
          return false;
      } else {
        // read a pointer to the data at 2*ptr_size
        count = process_sp->ReadUnsignedIntegerFromMemory(
            valobj_addr + 2 * ptr_size, ptr_size, 0, error);
        if (error.Fail())
          return false;
        // read the data at 2*ptr_size from the first location
        count = process_sp->ReadUnsignedIntegerFromMemory(count + 2 * ptr_size,
                                                          ptr_size, 0, error);
        if (error.Fail())
          return false;
      }
    } else
      return false;
  } while (false);
  stream.Printf("%" PRIu64 " index%s", count, (count == 1 ? "" : "es"));
  return true;
}

static void NSNumber_FormatChar(ValueObject &valobj, Stream &stream, char value,
                                lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:char");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%hhd%s", prefix.c_str(), value, suffix.c_str());
}

static void NSNumber_FormatShort(ValueObject &valobj, Stream &stream,
                                 short value, lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:short");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%hd%s", prefix.c_str(), value, suffix.c_str());
}

static void NSNumber_FormatInt(ValueObject &valobj, Stream &stream, int value,
                               lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:int");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%d%s", prefix.c_str(), value, suffix.c_str());
}

static void NSNumber_FormatLong(ValueObject &valobj, Stream &stream,
                                uint64_t value, lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:long");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%" PRId64 "%s", prefix.c_str(), value, suffix.c_str());
}

static void NSNumber_FormatInt128(ValueObject &valobj, Stream &stream,
                                 const llvm::APInt &value,
                                 lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:int128_t");
  
  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }
  
  stream.PutCString(prefix.c_str());
  const int radix = 10;
  const bool isSigned = true;
  std::string str = value.toString(radix, isSigned);
  stream.PutCString(str.c_str());
  stream.PutCString(suffix.c_str());
}

static void NSNumber_FormatFloat(ValueObject &valobj, Stream &stream,
                                 float value, lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:float");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%f%s", prefix.c_str(), value, suffix.c_str());
}

static void NSNumber_FormatDouble(ValueObject &valobj, Stream &stream,
                                  double value, lldb::LanguageType lang) {
  static ConstString g_TypeHint("NSNumber:double");

  std::string prefix, suffix;
  if (Language *language = Language::FindPlugin(lang)) {
    if (!language->GetFormatterPrefixSuffix(valobj, g_TypeHint, prefix,
                                            suffix)) {
      prefix.clear();
      suffix.clear();
    }
  }

  stream.Printf("%s%g%s", prefix.c_str(), value, suffix.c_str());
}

bool lldb_private::formatters::NSNumberSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name(descriptor->GetClassName().GetCString());

  if (class_name.empty())
    return false;

  if (class_name == "__NSCFBoolean")
    return ObjCBooleanSummaryProvider(valobj, stream, options);

  if (class_name == "NSDecimalNumber")
    return NSDecimalNumberSummaryProvider(valobj, stream, options);

  if (class_name == "NSNumber" || class_name == "__NSCFNumber") {
    uint64_t value = 0;
    uint64_t i_bits = 0;
    if (descriptor->GetTaggedPointerInfo(&i_bits, &value)) {
      switch (i_bits) {
      case 0:
        NSNumber_FormatChar(valobj, stream, (char)value, options.GetLanguage());
        break;
      case 1:
      case 4:
        NSNumber_FormatShort(valobj, stream, (short)value,
                             options.GetLanguage());
        break;
      case 2:
      case 8:
        NSNumber_FormatInt(valobj, stream, (int)value, options.GetLanguage());
        break;
      case 3:
      case 12:
        NSNumber_FormatLong(valobj, stream, value, options.GetLanguage());
        break;
      default:
        return false;
      }
      return true;
    } else {
      Status error;
      
      AppleObjCRuntime *runtime =
      llvm::dyn_cast_or_null<AppleObjCRuntime>(
          process_sp->GetObjCLanguageRuntime());

      const bool new_format =
          (runtime && runtime->GetFoundationVersion() >= 1400);

      enum class TypeCodes : int {
        sint8 = 0x0,
        sint16 = 0x1,
        sint32 = 0x2,
        sint64 = 0x3,
        f32 = 0x4,
        f64 = 0x5,
        sint128 = 0x6
      };
      
      uint64_t data_location = valobj_addr + 2 * ptr_size;
      TypeCodes type_code;
      
      if (new_format) {
        uint64_t cfinfoa =
            process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
        
        if (error.Fail())
          return false;

        bool is_preserved_number = cfinfoa & 0x8;
        if (is_preserved_number) {
          lldbassert(!static_cast<bool>("We should handle preserved numbers!"));
          return false;
        }

        type_code = static_cast<TypeCodes>(cfinfoa & 0x7);
      } else {
        uint8_t data_type =
        process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size, 1,
                                                  0, error) & 0x1F;
        
        if (error.Fail())
          return false;
        
        switch (data_type) {
          case 1: type_code = TypeCodes::sint8; break;
          case 2: type_code = TypeCodes::sint16; break;
          case 3: type_code = TypeCodes::sint32; break;
          case 17: data_location += 8; LLVM_FALLTHROUGH;
          case 4: type_code = TypeCodes::sint64; break;
          case 5: type_code = TypeCodes::f32; break;
          case 6: type_code = TypeCodes::f64; break;
          default: return false;
        }
      }
      
      uint64_t value = 0;
      bool success = false;
      switch (type_code) {
        case TypeCodes::sint8:
        value = process_sp->ReadUnsignedIntegerFromMemory(data_location, 1, 0,
                                                          error);
        if (error.Fail())
          return false;
        NSNumber_FormatChar(valobj, stream, (char)value, options.GetLanguage());
        success = true;
        break;
        case TypeCodes::sint16:
        value = process_sp->ReadUnsignedIntegerFromMemory(data_location, 2, 0,
                                                          error);
        if (error.Fail())
          return false;
        NSNumber_FormatShort(valobj, stream, (short)value,
                             options.GetLanguage());
        success = true;
        break;
      case TypeCodes::sint32:
        value = process_sp->ReadUnsignedIntegerFromMemory(data_location, 4, 0,
                                                          error);
        if (error.Fail())
          return false;
        NSNumber_FormatInt(valobj, stream, (int)value, options.GetLanguage());
        success = true;
        break;
      case TypeCodes::sint64:
        value = process_sp->ReadUnsignedIntegerFromMemory(data_location, 8, 0,
                                                          error);
        if (error.Fail())
          return false;
        NSNumber_FormatLong(valobj, stream, value, options.GetLanguage());
        success = true;
        break;
      case TypeCodes::f32:
      {
        uint32_t flt_as_int = process_sp->ReadUnsignedIntegerFromMemory(
            data_location, 4, 0, error);
        if (error.Fail())
          return false;
        float flt_value = 0.0f;
        memcpy(&flt_value, &flt_as_int, sizeof(flt_as_int));
        NSNumber_FormatFloat(valobj, stream, flt_value, options.GetLanguage());
        success = true;
        break;
      }
      case TypeCodes::f64:
      {
        uint64_t dbl_as_lng = process_sp->ReadUnsignedIntegerFromMemory(
            data_location, 8, 0, error);
        if (error.Fail())
          return false;
        double dbl_value = 0.0;
        memcpy(&dbl_value, &dbl_as_lng, sizeof(dbl_as_lng));
        NSNumber_FormatDouble(valobj, stream, dbl_value, options.GetLanguage());
        success = true;
        break;
      }
      case TypeCodes::sint128: // internally, this is the same
      {
        uint64_t words[2];
        words[1] = process_sp->ReadUnsignedIntegerFromMemory(
            data_location, 8, 0, error);
        if (error.Fail())
          return false;
        words[0] = process_sp->ReadUnsignedIntegerFromMemory(
            data_location + 8, 8, 0, error);
        if (error.Fail())
          return false;
        llvm::APInt i128_value(128, words);
        NSNumber_FormatInt128(valobj, stream, i128_value, options.GetLanguage());
        success = true;
        break;
      }
      }
      return success;
    }
  }

  return false;
}

bool lldb_private::formatters::NSDecimalNumberSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);
  uint32_t ptr_size = process_sp->GetAddressByteSize();

  Status error;
  int8_t exponent = process_sp->ReadUnsignedIntegerFromMemory(
      valobj_addr + ptr_size, 1, 0, error);
  if (error.Fail())
    return false;

  uint8_t length_and_negative = process_sp->ReadUnsignedIntegerFromMemory(
      valobj_addr + ptr_size + 1, 1, 0, error);
  if (error.Fail())
    return false;

  // Fifth bit marks negativity.
  const bool is_negative = (length_and_negative >> 4) & 1;

  // Zero length and negative means NaN.
  uint8_t length = length_and_negative & 0xf;
  const bool is_nan = is_negative && (length == 0);

  if (is_nan) {
    stream.Printf("NaN");
    return true;
  }

  if (length == 0) {
    stream.Printf("0");
    return true;
  }

  uint64_t mantissa = process_sp->ReadUnsignedIntegerFromMemory(
      valobj_addr + ptr_size + 4, 8, 0, error);
  if (error.Fail())
    return false;

  if (is_negative)
    stream.Printf("-");

  stream.Printf("%" PRIu64 " x 10^%" PRIi8, mantissa, exponent);
  return true;
}

bool lldb_private::formatters::NSURLSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  llvm::StringRef class_name = descriptor->GetClassName().GetStringRef();

  if (!class_name.equals("NSURL"))
    return false;

  uint64_t offset_text = ptr_size + ptr_size +
                         8; // ISA + pointer + 8 bytes of data (even on 32bit)
  uint64_t offset_base = offset_text + ptr_size;
  CompilerType type(valobj.GetCompilerType());
  ValueObjectSP text(valobj.GetSyntheticChildAtOffset(offset_text, type, true));
  ValueObjectSP base(valobj.GetSyntheticChildAtOffset(offset_base, type, true));
  if (!text)
    return false;
  if (text->GetValueAsUnsigned(0) == 0)
    return false;
  StreamString summary;
  if (!NSStringSummaryProvider(*text, summary, options))
    return false;
  if (base && base->GetValueAsUnsigned(0)) {
    std::string summary_str = summary.GetString();

    if (!summary_str.empty())
      summary_str.pop_back();
    summary_str += " -- ";
    StreamString base_summary;
    if (NSURLSummaryProvider(*base, base_summary, options) &&
        !base_summary.Empty()) {
      llvm::StringRef base_str = base_summary.GetString();
      if (base_str.size() > 2)
        base_str = base_str.drop_front(2);
      summary_str += base_str;
    }
    summary.Clear();
    summary.PutCString(summary_str);
  }
  if (!summary.Empty()) {
    stream.PutCString(summary.GetString());
    return true;
  }

  return false;
}

/// Bias value for tagged pointer exponents.
/// Recommended values:
/// 0x3e3: encodes all dates between distantPast and distantFuture
///   except for the range within about 1e-28 second of the reference date.
/// 0x3ef: encodes all dates for a few million years beyond distantPast and
///   distantFuture, except within about 1e-25 second of the reference date.
const int TAGGED_DATE_EXPONENT_BIAS = 0x3ef;

typedef union {
  struct {
    uint64_t fraction:52;  // unsigned
    uint64_t exponent:11;  // signed
    uint64_t sign:1;
  };
  uint64_t i;
  double d;
} DoubleBits;
typedef union {
  struct {
    uint64_t fraction:52;  // unsigned
    uint64_t exponent:7;   // signed
    uint64_t sign:1;
    uint64_t unused:4;  // placeholder for pointer tag bits
  };
  uint64_t i;
} TaggedDoubleBits;

static uint64_t decodeExponent(uint64_t exp) {
  // Tagged exponent field is 7-bit signed. Sign-extend the value to 64 bits
  // before performing arithmetic.
  return llvm::SignExtend64<7>(exp) + TAGGED_DATE_EXPONENT_BIAS;
}

static uint64_t decodeTaggedTimeInterval(uint64_t encodedTimeInterval) {
  if (encodedTimeInterval == 0)
    return 0.0;
  if (encodedTimeInterval == std::numeric_limits<uint64_t>::max())
    return (uint64_t)-0.0;

  TaggedDoubleBits encodedBits = {};
  encodedBits.i = encodedTimeInterval;
  DoubleBits decodedBits;

  // Sign and fraction are represented exactly.
  // Exponent is encoded.
  assert(encodedBits.unused == 0);
  decodedBits.sign = encodedBits.sign;
  decodedBits.fraction = encodedBits.fraction;
  decodedBits.exponent = decodeExponent(encodedBits.exponent);

  return decodedBits.d;
}

bool lldb_private::formatters::NSDateSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  uint64_t date_value_bits = 0;
  double date_value = 0.0;

  ConstString class_name = descriptor->GetClassName();

  static const ConstString g_NSDate("NSDate");
  static const ConstString g___NSDate("__NSDate");
  static const ConstString g___NSTaggedDate("__NSTaggedDate");
  static const ConstString g_NSCalendarDate("NSCalendarDate");

  if (class_name.IsEmpty())
    return false;

  uint64_t info_bits = 0, value_bits = 0;
  if ((class_name == g_NSDate) || (class_name == g___NSDate) ||
      (class_name == g___NSTaggedDate)) {
    if (descriptor->GetTaggedPointerInfo(&info_bits, &value_bits)) {
      date_value_bits = ((value_bits << 8) | (info_bits << 4));
      memcpy(&date_value, &date_value_bits, sizeof(date_value_bits));
    } else {
      llvm::Triple triple(
          process_sp->GetTarget().GetArchitecture().GetTriple());
      uint32_t delta =
          (triple.isWatchOS() && triple.isWatchABI()) ? 8 : ptr_size;
      Status error;
      date_value_bits = process_sp->ReadUnsignedIntegerFromMemory(
          valobj_addr + delta, 8, 0, error);
      memcpy(&date_value, &date_value_bits, sizeof(date_value_bits));
      if (error.Fail())
        return false;
    }
  } else if (class_name == g_NSCalendarDate) {
    Status error;
    date_value_bits = process_sp->ReadUnsignedIntegerFromMemory(
        valobj_addr + 2 * ptr_size, 8, 0, error);
    memcpy(&date_value, &date_value_bits, sizeof(date_value_bits));
    if (error.Fail())
      return false;
  } else
    return false;

  if (date_value == -63114076800) {
    stream.Printf("0001-12-30 00:00:00 +0000");
    return true;
  }

  // Accomodate for the __NSTaggedDate format introduced in Foundation 1600.
  if (class_name == g___NSTaggedDate) {
    auto *runtime = llvm::dyn_cast_or_null<AppleObjCRuntime>(process_sp->GetObjCLanguageRuntime());
    if (runtime && runtime->GetFoundationVersion() >= 1600)
      date_value = decodeTaggedTimeInterval(value_bits << 4);
  }

  // this snippet of code assumes that time_t == seconds since Jan-1-1970 this
  // is generally true and POSIXly happy, but might break if a library vendor
  // decides to get creative
  time_t epoch = GetOSXEpoch();
  epoch = epoch + (time_t)date_value;
  tm *tm_date = gmtime(&epoch);
  if (!tm_date)
    return false;
  std::string buffer(1024, 0);
  if (strftime(&buffer[0], 1023, "%Z", tm_date) == 0)
    return false;
  stream.Printf("%04d-%02d-%02d %02d:%02d:%02d %s", tm_date->tm_year + 1900,
                tm_date->tm_mon + 1, tm_date->tm_mday, tm_date->tm_hour,
                tm_date->tm_min, tm_date->tm_sec, buffer.c_str());
  return true;
}

bool lldb_private::formatters::ObjCClassSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptorFromISA(valobj.GetValueAsUnsigned(0)));

  if (!descriptor || !descriptor->IsValid())
    return false;

  ConstString class_name = descriptor->GetClassName();

  if (class_name.IsEmpty())
    return false;

  if (ConstString cs =
          Mangled(class_name).GetDemangledName(lldb::eLanguageTypeUnknown))
    class_name = cs;

  stream.Printf("%s", class_name.AsCString("<unknown class>"));
  return true;
}

class ObjCClassSyntheticChildrenFrontEnd : public SyntheticChildrenFrontEnd {
public:
  ObjCClassSyntheticChildrenFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp) {}

  ~ObjCClassSyntheticChildrenFrontEnd() override = default;

  size_t CalculateNumChildren() override { return 0; }

  lldb::ValueObjectSP GetChildAtIndex(size_t idx) override {
    return lldb::ValueObjectSP();
  }

  bool Update() override { return false; }

  bool MightHaveChildren() override { return false; }

  size_t GetIndexOfChildWithName(const ConstString &name) override {
    return UINT32_MAX;
  }
};

SyntheticChildrenFrontEnd *
lldb_private::formatters::ObjCClassSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return new ObjCClassSyntheticChildrenFrontEnd(valobj_sp);
}

template <bool needs_at>
bool lldb_private::formatters::NSDataSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime =
      (ObjCLanguageRuntime *)process_sp->GetLanguageRuntime(
          lldb::eLanguageTypeObjC);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  bool is_64bit = (process_sp->GetAddressByteSize() == 8);
  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  uint64_t value = 0;

  llvm::StringRef class_name = descriptor->GetClassName().GetCString();

  if (class_name.empty())
    return false;

  bool isNSConcreteData = class_name == "NSConcreteData";
  bool isNSConcreteMutableData = class_name == "NSConcreteMutableData";
  bool isNSCFData = class_name == "__NSCFData";
  if (isNSConcreteData || isNSConcreteMutableData || isNSCFData) {
    uint32_t offset;
    if (isNSConcreteData)
      offset = is_64bit ? 8 : 4;
    else
      offset = is_64bit ? 16 : 8;

    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(
        valobj_addr + offset, is_64bit ? 8 : 4, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == "_NSInlineData") {
    uint32_t offset = (is_64bit ? 8 : 4);
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + offset, 2,
                                                      0, error);
    if (error.Fail())
      return false;
  } else if (class_name == "_NSZeroData") {
    value = 0;
  } else
    return false;

  stream.Printf("%s%" PRIu64 " byte%s%s", (needs_at ? "@\"" : ""), value,
                (value != 1 ? "s" : ""), (needs_at ? "\"" : ""));

  return true;
}

bool lldb_private::formatters::ObjCBOOLSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  const uint32_t type_info = valobj.GetCompilerType().GetTypeInfo();

  ValueObjectSP real_guy_sp = valobj.GetSP();

  if (type_info & eTypeIsPointer) {
    Status err;
    real_guy_sp = valobj.Dereference(err);
    if (err.Fail() || !real_guy_sp)
      return false;
  } else if (type_info & eTypeIsReference) {
    real_guy_sp = valobj.GetChildAtIndex(0, true);
    if (!real_guy_sp)
      return false;
  }
  uint8_t value = (real_guy_sp->GetValueAsUnsigned(0) & 0xFF);
  switch (value) {
  case 0:
    stream.Printf("NO");
    break;
  case 1:
    stream.Printf("YES");
    break;
  default:
    stream.Printf("%u", value);
    break;
  }
  return true;
}

bool lldb_private::formatters::ObjCBooleanSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  lldb::addr_t valobj_ptr_value =
      valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
  if (valobj_ptr_value == LLDB_INVALID_ADDRESS)
    return false;

  ProcessSP process_sp(valobj.GetProcessSP());
  if (!process_sp)
    return false;

  if (AppleObjCRuntime *objc_runtime =
          (AppleObjCRuntime *)process_sp->GetObjCLanguageRuntime()) {
    lldb::addr_t cf_true = LLDB_INVALID_ADDRESS,
                 cf_false = LLDB_INVALID_ADDRESS;
    objc_runtime->GetValuesForGlobalCFBooleans(cf_true, cf_false);
    if (valobj_ptr_value == cf_true) {
      stream.PutCString("YES");
      return true;
    }
    if (valobj_ptr_value == cf_false) {
      stream.PutCString("NO");
      return true;
    }
  }

  return false;
}

template <bool is_sel_ptr>
bool lldb_private::formatters::ObjCSELSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  lldb::ValueObjectSP valobj_sp;

  CompilerType charstar(valobj.GetCompilerType()
                            .GetBasicTypeFromAST(eBasicTypeChar)
                            .GetPointerType());

  if (!charstar)
    return false;

  ExecutionContext exe_ctx(valobj.GetExecutionContextRef());

  if (is_sel_ptr) {
    lldb::addr_t data_address = valobj.GetValueAsUnsigned(LLDB_INVALID_ADDRESS);
    if (data_address == LLDB_INVALID_ADDRESS)
      return false;
    valobj_sp = ValueObject::CreateValueObjectFromAddress("text", data_address,
                                                          exe_ctx, charstar);
  } else {
    DataExtractor data;
    Status error;
    valobj.GetData(data, error);
    if (error.Fail())
      return false;
    valobj_sp =
        ValueObject::CreateValueObjectFromData("text", data, exe_ctx, charstar);
  }

  if (!valobj_sp)
    return false;

  stream.Printf("%s", valobj_sp->GetSummaryAsCString());
  return true;
}

// POSIX has an epoch on Jan-1-1970, but Cocoa prefers Jan-1-2001
// this call gives the POSIX equivalent of the Cocoa epoch
time_t lldb_private::formatters::GetOSXEpoch() {
  static time_t epoch = 0;
  if (!epoch) {
#ifndef _WIN32
    tzset();
    tm tm_epoch;
    tm_epoch.tm_sec = 0;
    tm_epoch.tm_hour = 0;
    tm_epoch.tm_min = 0;
    tm_epoch.tm_mon = 0;
    tm_epoch.tm_mday = 1;
    tm_epoch.tm_year = 2001 - 1900;
    tm_epoch.tm_isdst = -1;
    tm_epoch.tm_gmtoff = 0;
    tm_epoch.tm_zone = nullptr;
    epoch = timegm(&tm_epoch);
#endif
  }
  return epoch;
}

template bool lldb_private::formatters::NSDataSummaryProvider<true>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

template bool lldb_private::formatters::NSDataSummaryProvider<false>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

template bool lldb_private::formatters::ObjCSELSummaryProvider<true>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

template bool lldb_private::formatters::ObjCSELSummaryProvider<false>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

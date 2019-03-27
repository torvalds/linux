//===-- DumpDataExtractor.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DumpDataExtractor.h"

#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Stream.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/CanonicalType.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <limits>
#include <memory>
#include <string>

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>

#include <bitset>
#include <sstream>

using namespace lldb_private;
using namespace lldb;

#define NON_PRINTABLE_CHAR '.'

static float half2float(uint16_t half) {
  union {
    float f;
    uint32_t u;
  } u;
  int32_t v = (int16_t)half;

  if (0 == (v & 0x7c00)) {
    u.u = v & 0x80007FFFU;
    return u.f * ldexpf(1, 125);
  }

  v <<= 13;
  u.u = v | 0x70000000U;
  return u.f * ldexpf(1, -112);
}

static bool GetAPInt(const DataExtractor &data, lldb::offset_t *offset_ptr,
                     lldb::offset_t byte_size, llvm::APInt &result) {
  llvm::SmallVector<uint64_t, 2> uint64_array;
  lldb::offset_t bytes_left = byte_size;
  uint64_t u64;
  const lldb::ByteOrder byte_order = data.GetByteOrder();
  if (byte_order == lldb::eByteOrderLittle) {
    while (bytes_left > 0) {
      if (bytes_left >= 8) {
        u64 = data.GetU64(offset_ptr);
        bytes_left -= 8;
      } else {
        u64 = data.GetMaxU64(offset_ptr, (uint32_t)bytes_left);
        bytes_left = 0;
      }
      uint64_array.push_back(u64);
    }
    result = llvm::APInt(byte_size * 8, llvm::ArrayRef<uint64_t>(uint64_array));
    return true;
  } else if (byte_order == lldb::eByteOrderBig) {
    lldb::offset_t be_offset = *offset_ptr + byte_size;
    lldb::offset_t temp_offset;
    while (bytes_left > 0) {
      if (bytes_left >= 8) {
        be_offset -= 8;
        temp_offset = be_offset;
        u64 = data.GetU64(&temp_offset);
        bytes_left -= 8;
      } else {
        be_offset -= bytes_left;
        temp_offset = be_offset;
        u64 = data.GetMaxU64(&temp_offset, (uint32_t)bytes_left);
        bytes_left = 0;
      }
      uint64_array.push_back(u64);
    }
    *offset_ptr += byte_size;
    result = llvm::APInt(byte_size * 8, llvm::ArrayRef<uint64_t>(uint64_array));
    return true;
  }
  return false;
}

static lldb::offset_t DumpAPInt(Stream *s, const DataExtractor &data,
                                lldb::offset_t offset, lldb::offset_t byte_size,
                                bool is_signed, unsigned radix) {
  llvm::APInt apint;
  if (GetAPInt(data, &offset, byte_size, apint)) {
    std::string apint_str(apint.toString(radix, is_signed));
    switch (radix) {
    case 2:
      s->Write("0b", 2);
      break;
    case 8:
      s->Write("0", 1);
      break;
    case 10:
      break;
    }
    s->Write(apint_str.c_str(), apint_str.size());
  }
  return offset;
}

lldb::offset_t lldb_private::DumpDataExtractor(
    const DataExtractor &DE, Stream *s, offset_t start_offset,
    lldb::Format item_format, size_t item_byte_size, size_t item_count,
    size_t num_per_line, uint64_t base_addr,
    uint32_t item_bit_size,   // If zero, this is not a bitfield value, if
                              // non-zero, the value is a bitfield
    uint32_t item_bit_offset, // If "item_bit_size" is non-zero, this is the
                              // shift amount to apply to a bitfield
    ExecutionContextScope *exe_scope) {
  if (s == nullptr)
    return start_offset;

  if (item_format == eFormatPointer) {
    if (item_byte_size != 4 && item_byte_size != 8)
      item_byte_size = s->GetAddressByteSize();
  }

  offset_t offset = start_offset;

  if (item_format == eFormatInstruction) {
    TargetSP target_sp;
    if (exe_scope)
      target_sp = exe_scope->CalculateTarget();
    if (target_sp) {
      DisassemblerSP disassembler_sp(Disassembler::FindPlugin(
          target_sp->GetArchitecture(),
          target_sp->GetDisassemblyFlavor(), nullptr));
      if (disassembler_sp) {
        lldb::addr_t addr = base_addr + start_offset;
        lldb_private::Address so_addr;
        bool data_from_file = true;
        if (target_sp->GetSectionLoadList().ResolveLoadAddress(addr, so_addr)) {
          data_from_file = false;
        } else {
          if (target_sp->GetSectionLoadList().IsEmpty() ||
              !target_sp->GetImages().ResolveFileAddress(addr, so_addr))
            so_addr.SetRawAddress(addr);
        }

        size_t bytes_consumed = disassembler_sp->DecodeInstructions(
            so_addr, DE, start_offset, item_count, false, data_from_file);

        if (bytes_consumed) {
          offset += bytes_consumed;
          const bool show_address = base_addr != LLDB_INVALID_ADDRESS;
          const bool show_bytes = true;
          ExecutionContext exe_ctx;
          exe_scope->CalculateExecutionContext(exe_ctx);
          disassembler_sp->GetInstructionList().Dump(s, show_address,
                                                     show_bytes, &exe_ctx);
        }
      }
    } else
      s->Printf("invalid target");

    return offset;
  }

  if ((item_format == eFormatOSType || item_format == eFormatAddressInfo) &&
      item_byte_size > 8)
    item_format = eFormatHex;

  lldb::offset_t line_start_offset = start_offset;
  for (uint32_t count = 0; DE.ValidOffset(offset) && count < item_count;
       ++count) {
    if ((count % num_per_line) == 0) {
      if (count > 0) {
        if (item_format == eFormatBytesWithASCII &&
            offset > line_start_offset) {
          s->Printf("%*s",
                    static_cast<int>(
                        (num_per_line - (offset - line_start_offset)) * 3 + 2),
                    "");
          DumpDataExtractor(DE, s, line_start_offset, eFormatCharPrintable, 1,
                            offset - line_start_offset, SIZE_MAX,
                            LLDB_INVALID_ADDRESS, 0, 0);
        }
        s->EOL();
      }
      if (base_addr != LLDB_INVALID_ADDRESS)
        s->Printf("0x%8.8" PRIx64 ": ",
                  (uint64_t)(base_addr +
                             (offset - start_offset) / DE.getTargetByteSize()));

      line_start_offset = offset;
    } else if (item_format != eFormatChar &&
               item_format != eFormatCharPrintable &&
               item_format != eFormatCharArray && count > 0) {
      s->PutChar(' ');
    }

    switch (item_format) {
    case eFormatBoolean:
      if (item_byte_size <= 8)
        s->Printf("%s", DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                             item_bit_size, item_bit_offset)
                            ? "true"
                            : "false");
      else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for boolean format",
                  (uint64_t)item_byte_size);
        return offset;
      }
      break;

    case eFormatBinary:
      if (item_byte_size <= 8) {
        uint64_t uval64 = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                               item_bit_size, item_bit_offset);
        // Avoid std::bitset<64>::to_string() since it is missing in earlier
        // C++ libraries
        std::string binary_value(64, '0');
        std::bitset<64> bits(uval64);
        for (uint32_t i = 0; i < 64; ++i)
          if (bits[i])
            binary_value[64 - 1 - i] = '1';
        if (item_bit_size > 0)
          s->Printf("0b%s", binary_value.c_str() + 64 - item_bit_size);
        else if (item_byte_size > 0 && item_byte_size <= 8)
          s->Printf("0b%s", binary_value.c_str() + 64 - item_byte_size * 8);
      } else {
        const bool is_signed = false;
        const unsigned radix = 2;
        offset = DumpAPInt(s, DE, offset, item_byte_size, is_signed, radix);
      }
      break;

    case eFormatBytes:
    case eFormatBytesWithASCII:
      for (uint32_t i = 0; i < item_byte_size; ++i) {
        s->Printf("%2.2x", DE.GetU8(&offset));
      }

      // Put an extra space between the groups of bytes if more than one is
      // being dumped in a group (item_byte_size is more than 1).
      if (item_byte_size > 1)
        s->PutChar(' ');
      break;

    case eFormatChar:
    case eFormatCharPrintable:
    case eFormatCharArray: {
      // Reject invalid item_byte_size.
      if (item_byte_size > 8) {
        s->Printf("error: unsupported byte size (%" PRIu64 ") for char format",
                  (uint64_t)item_byte_size);
        return offset;
      }

      // If we are only printing one character surround it with single quotes
      if (item_count == 1 && item_format == eFormatChar)
        s->PutChar('\'');

      const uint64_t ch = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                               item_bit_size, item_bit_offset);
      if (isprint(ch))
        s->Printf("%c", (char)ch);
      else if (item_format != eFormatCharPrintable) {
        switch (ch) {
        case '\033':
          s->Printf("\\e");
          break;
        case '\a':
          s->Printf("\\a");
          break;
        case '\b':
          s->Printf("\\b");
          break;
        case '\f':
          s->Printf("\\f");
          break;
        case '\n':
          s->Printf("\\n");
          break;
        case '\r':
          s->Printf("\\r");
          break;
        case '\t':
          s->Printf("\\t");
          break;
        case '\v':
          s->Printf("\\v");
          break;
        case '\0':
          s->Printf("\\0");
          break;
        default:
          if (item_byte_size == 1)
            s->Printf("\\x%2.2x", (uint8_t)ch);
          else
            s->Printf("%" PRIu64, ch);
          break;
        }
      } else {
        s->PutChar(NON_PRINTABLE_CHAR);
      }

      // If we are only printing one character surround it with single quotes
      if (item_count == 1 && item_format == eFormatChar)
        s->PutChar('\'');
    } break;

    case eFormatEnum: // Print enum value as a signed integer when we don't get
                      // the enum type
    case eFormatDecimal:
      if (item_byte_size <= 8)
        s->Printf("%" PRId64,
                  DE.GetMaxS64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      else {
        const bool is_signed = true;
        const unsigned radix = 10;
        offset = DumpAPInt(s, DE, offset, item_byte_size, is_signed, radix);
      }
      break;

    case eFormatUnsigned:
      if (item_byte_size <= 8)
        s->Printf("%" PRIu64,
                  DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      else {
        const bool is_signed = false;
        const unsigned radix = 10;
        offset = DumpAPInt(s, DE, offset, item_byte_size, is_signed, radix);
      }
      break;

    case eFormatOctal:
      if (item_byte_size <= 8)
        s->Printf("0%" PRIo64,
                  DE.GetMaxS64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      else {
        const bool is_signed = false;
        const unsigned radix = 8;
        offset = DumpAPInt(s, DE, offset, item_byte_size, is_signed, radix);
      }
      break;

    case eFormatOSType: {
      uint64_t uval64 = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                             item_bit_size, item_bit_offset);
      s->PutChar('\'');
      for (uint32_t i = 0; i < item_byte_size; ++i) {
        uint8_t ch = (uint8_t)(uval64 >> ((item_byte_size - i - 1) * 8));
        if (isprint(ch))
          s->Printf("%c", ch);
        else {
          switch (ch) {
          case '\033':
            s->Printf("\\e");
            break;
          case '\a':
            s->Printf("\\a");
            break;
          case '\b':
            s->Printf("\\b");
            break;
          case '\f':
            s->Printf("\\f");
            break;
          case '\n':
            s->Printf("\\n");
            break;
          case '\r':
            s->Printf("\\r");
            break;
          case '\t':
            s->Printf("\\t");
            break;
          case '\v':
            s->Printf("\\v");
            break;
          case '\0':
            s->Printf("\\0");
            break;
          default:
            s->Printf("\\x%2.2x", ch);
            break;
          }
        }
      }
      s->PutChar('\'');
    } break;

    case eFormatCString: {
      const char *cstr = DE.GetCStr(&offset);

      if (!cstr) {
        s->Printf("NULL");
        offset = LLDB_INVALID_OFFSET;
      } else {
        s->PutChar('\"');

        while (const char c = *cstr) {
          if (isprint(c)) {
            s->PutChar(c);
          } else {
            switch (c) {
            case '\033':
              s->Printf("\\e");
              break;
            case '\a':
              s->Printf("\\a");
              break;
            case '\b':
              s->Printf("\\b");
              break;
            case '\f':
              s->Printf("\\f");
              break;
            case '\n':
              s->Printf("\\n");
              break;
            case '\r':
              s->Printf("\\r");
              break;
            case '\t':
              s->Printf("\\t");
              break;
            case '\v':
              s->Printf("\\v");
              break;
            default:
              s->Printf("\\x%2.2x", c);
              break;
            }
          }

          ++cstr;
        }

        s->PutChar('\"');
      }
    } break;

    case eFormatPointer:
      s->Address(DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                      item_bit_offset),
                 sizeof(addr_t));
      break;

    case eFormatComplexInteger: {
      size_t complex_int_byte_size = item_byte_size / 2;

      if (complex_int_byte_size > 0 && complex_int_byte_size <= 8) {
        s->Printf("%" PRIu64,
                  DE.GetMaxU64Bitfield(&offset, complex_int_byte_size, 0, 0));
        s->Printf(" + %" PRIu64 "i",
                  DE.GetMaxU64Bitfield(&offset, complex_int_byte_size, 0, 0));
      } else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for complex integer format",
                  (uint64_t)item_byte_size);
        return offset;
      }
    } break;

    case eFormatComplex:
      if (sizeof(float) * 2 == item_byte_size) {
        float f32_1 = DE.GetFloat(&offset);
        float f32_2 = DE.GetFloat(&offset);

        s->Printf("%g + %gi", f32_1, f32_2);
        break;
      } else if (sizeof(double) * 2 == item_byte_size) {
        double d64_1 = DE.GetDouble(&offset);
        double d64_2 = DE.GetDouble(&offset);

        s->Printf("%lg + %lgi", d64_1, d64_2);
        break;
      } else if (sizeof(long double) * 2 == item_byte_size) {
        long double ld64_1 = DE.GetLongDouble(&offset);
        long double ld64_2 = DE.GetLongDouble(&offset);
        s->Printf("%Lg + %Lgi", ld64_1, ld64_2);
        break;
      } else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for complex float format",
                  (uint64_t)item_byte_size);
        return offset;
      }
      break;

    default:
    case eFormatDefault:
    case eFormatHex:
    case eFormatHexUppercase: {
      bool wantsuppercase = (item_format == eFormatHexUppercase);
      switch (item_byte_size) {
      case 1:
      case 2:
      case 4:
      case 8:
        s->Printf(wantsuppercase ? "0x%*.*" PRIX64 : "0x%*.*" PRIx64,
                  (int)(2 * item_byte_size), (int)(2 * item_byte_size),
                  DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
        break;
      default: {
        assert(item_bit_size == 0 && item_bit_offset == 0);
        const uint8_t *bytes =
            (const uint8_t *)DE.GetData(&offset, item_byte_size);
        if (bytes) {
          s->PutCString("0x");
          uint32_t idx;
          if (DE.GetByteOrder() == eByteOrderBig) {
            for (idx = 0; idx < item_byte_size; ++idx)
              s->Printf(wantsuppercase ? "%2.2X" : "%2.2x", bytes[idx]);
          } else {
            for (idx = 0; idx < item_byte_size; ++idx)
              s->Printf(wantsuppercase ? "%2.2X" : "%2.2x",
                        bytes[item_byte_size - 1 - idx]);
          }
        }
      } break;
      }
    } break;

    case eFormatFloat: {
      TargetSP target_sp;
      bool used_apfloat = false;
      if (exe_scope)
        target_sp = exe_scope->CalculateTarget();
      if (target_sp) {
        ClangASTContext *clang_ast = target_sp->GetScratchClangASTContext();
        if (clang_ast) {
          clang::ASTContext *ast = clang_ast->getASTContext();
          if (ast) {
            llvm::SmallVector<char, 256> sv;
            // Show full precision when printing float values
            const unsigned format_precision = 0;
            const unsigned format_max_padding = 100;
            size_t item_bit_size = item_byte_size * 8;

            if (item_bit_size == ast->getTypeSize(ast->FloatTy)) {
              llvm::APInt apint(item_bit_size,
                                DE.GetMaxU64(&offset, item_byte_size));
              llvm::APFloat apfloat(ast->getFloatTypeSemantics(ast->FloatTy),
                                    apint);
              apfloat.toString(sv, format_precision, format_max_padding);
            } else if (item_bit_size == ast->getTypeSize(ast->DoubleTy)) {
              llvm::APInt apint;
              if (GetAPInt(DE, &offset, item_byte_size, apint)) {
                llvm::APFloat apfloat(ast->getFloatTypeSemantics(ast->DoubleTy),
                                      apint);
                apfloat.toString(sv, format_precision, format_max_padding);
              }
            } else if (item_bit_size == ast->getTypeSize(ast->LongDoubleTy)) {
              const auto &semantics =
                  ast->getFloatTypeSemantics(ast->LongDoubleTy);

              offset_t byte_size = item_byte_size;
              if (&semantics == &llvm::APFloatBase::x87DoubleExtended())
                byte_size = (llvm::APFloat::getSizeInBits(semantics) + 7) / 8;

              llvm::APInt apint;
              if (GetAPInt(DE, &offset, byte_size, apint)) {
                llvm::APFloat apfloat(semantics, apint);
                apfloat.toString(sv, format_precision, format_max_padding);
              }
            } else if (item_bit_size == ast->getTypeSize(ast->HalfTy)) {
              llvm::APInt apint(item_bit_size, DE.GetU16(&offset));
              llvm::APFloat apfloat(ast->getFloatTypeSemantics(ast->HalfTy),
                                    apint);
              apfloat.toString(sv, format_precision, format_max_padding);
            }

            if (!sv.empty()) {
              s->Printf("%*.*s", (int)sv.size(), (int)sv.size(), sv.data());
              used_apfloat = true;
            }
          }
        }
      }

      if (!used_apfloat) {
        std::ostringstream ss;
        if (item_byte_size == sizeof(float) || item_byte_size == 2) {
          float f;
          if (item_byte_size == 2) {
            uint16_t half = DE.GetU16(&offset);
            f = half2float(half);
          } else {
            f = DE.GetFloat(&offset);
          }
          ss.precision(std::numeric_limits<float>::digits10);
          ss << f;
        } else if (item_byte_size == sizeof(double)) {
          ss.precision(std::numeric_limits<double>::digits10);
          ss << DE.GetDouble(&offset);
        } else if (item_byte_size == sizeof(long double) ||
                   item_byte_size == 10) {
          ss.precision(std::numeric_limits<long double>::digits10);
          ss << DE.GetLongDouble(&offset);
        } else {
          s->Printf("error: unsupported byte size (%" PRIu64
                    ") for float format",
                    (uint64_t)item_byte_size);
          return offset;
        }
        ss.flush();
        s->Printf("%s", ss.str().c_str());
      }
    } break;

    case eFormatUnicode16:
      s->Printf("U+%4.4x", DE.GetU16(&offset));
      break;

    case eFormatUnicode32:
      s->Printf("U+0x%8.8x", DE.GetU32(&offset));
      break;

    case eFormatAddressInfo: {
      addr_t addr = DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                         item_bit_offset);
      s->Printf("0x%*.*" PRIx64, (int)(2 * item_byte_size),
                (int)(2 * item_byte_size), addr);
      if (exe_scope) {
        TargetSP target_sp(exe_scope->CalculateTarget());
        lldb_private::Address so_addr;
        if (target_sp) {
          if (target_sp->GetSectionLoadList().ResolveLoadAddress(addr,
                                                                 so_addr)) {
            s->PutChar(' ');
            so_addr.Dump(s, exe_scope, Address::DumpStyleResolvedDescription,
                         Address::DumpStyleModuleWithFileAddress);
          } else {
            so_addr.SetOffset(addr);
            so_addr.Dump(s, exe_scope,
                         Address::DumpStyleResolvedPointerDescription);
          }
        }
      }
    } break;

    case eFormatHexFloat:
      if (sizeof(float) == item_byte_size) {
        char float_cstr[256];
        llvm::APFloat ap_float(DE.GetFloat(&offset));
        ap_float.convertToHexString(float_cstr, 0, false,
                                    llvm::APFloat::rmNearestTiesToEven);
        s->Printf("%s", float_cstr);
        break;
      } else if (sizeof(double) == item_byte_size) {
        char float_cstr[256];
        llvm::APFloat ap_float(DE.GetDouble(&offset));
        ap_float.convertToHexString(float_cstr, 0, false,
                                    llvm::APFloat::rmNearestTiesToEven);
        s->Printf("%s", float_cstr);
        break;
      } else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for hex float format",
                  (uint64_t)item_byte_size);
        return offset;
      }
      break;

    // please keep the single-item formats below in sync with
    // FormatManager::GetSingleItemFormat if you fail to do so, users will
    // start getting different outputs depending on internal implementation
    // details they should not care about ||
    case eFormatVectorOfChar: //   ||
      s->PutChar('{');        //   \/
      offset =
          DumpDataExtractor(DE, s, offset, eFormatCharArray, 1, item_byte_size,
                            item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt8:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatDecimal, 1, item_byte_size,
                            item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt8:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, 1, item_byte_size,
                                 item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt16:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint16_t),
          item_byte_size / sizeof(uint16_t), item_byte_size / sizeof(uint16_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt16:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint16_t),
                                 item_byte_size / sizeof(uint16_t),
                                 item_byte_size / sizeof(uint16_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt32:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint32_t),
          item_byte_size / sizeof(uint32_t), item_byte_size / sizeof(uint32_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt32:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint32_t),
                                 item_byte_size / sizeof(uint32_t),
                                 item_byte_size / sizeof(uint32_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt64:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint64_t),
          item_byte_size / sizeof(uint64_t), item_byte_size / sizeof(uint64_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt64:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint64_t),
                                 item_byte_size / sizeof(uint64_t),
                                 item_byte_size / sizeof(uint64_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat16:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 2, item_byte_size / 2,
                            item_byte_size / 2, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat32:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 4, item_byte_size / 4,
                            item_byte_size / 4, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat64:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 8, item_byte_size / 8,
                            item_byte_size / 8, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt128:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatHex, 16, item_byte_size / 16,
                            item_byte_size / 16, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;
    }
  }

  if (item_format == eFormatBytesWithASCII && offset > line_start_offset) {
    s->Printf("%*s", static_cast<int>(
                         (num_per_line - (offset - line_start_offset)) * 3 + 2),
              "");
    DumpDataExtractor(DE, s, line_start_offset, eFormatCharPrintable, 1,
                      offset - line_start_offset, SIZE_MAX,
                      LLDB_INVALID_ADDRESS, 0, 0);
  }
  return offset; // Return the offset at which we ended up
}

void lldb_private::DumpHexBytes(Stream *s, const void *src, size_t src_len,
                                uint32_t bytes_per_line,
                                lldb::addr_t base_addr) {
  DataExtractor data(src, src_len, lldb::eByteOrderLittle, 4);
  DumpDataExtractor(data, s,
                    0,                  // Offset into "src"
                    lldb::eFormatBytes, // Dump as hex bytes
                    1,              // Size of each item is 1 for single bytes
                    src_len,        // Number of bytes
                    bytes_per_line, // Num bytes per line
                    base_addr,      // Base address
                    0, 0);          // Bitfield info
}

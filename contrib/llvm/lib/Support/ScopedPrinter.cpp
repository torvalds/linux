#include "llvm/Support/ScopedPrinter.h"

#include "llvm/Support/Format.h"
#include <cctype>

using namespace llvm::support;

namespace llvm {

raw_ostream &operator<<(raw_ostream &OS, const HexNumber &Value) {
  OS << "0x" << to_hexString(Value.Value);
  return OS;
}

const std::string to_hexString(uint64_t Value, bool UpperCase) {
  std::string number;
  llvm::raw_string_ostream stream(number);
  stream << format_hex_no_prefix(Value, 1, UpperCase);
  return stream.str();
}

void ScopedPrinter::printBinaryImpl(StringRef Label, StringRef Str,
                                    ArrayRef<uint8_t> Data, bool Block,
                                    uint32_t StartOffset) {
  if (Data.size() > 16)
    Block = true;

  if (Block) {
    startLine() << Label;
    if (!Str.empty())
      OS << ": " << Str;
    OS << " (\n";
    if (!Data.empty())
      OS << format_bytes_with_ascii(Data, StartOffset, 16, 4,
                                    (IndentLevel + 1) * 2, true)
         << "\n";
    startLine() << ")\n";
  } else {
    startLine() << Label << ":";
    if (!Str.empty())
      OS << " " << Str;
    OS << " (" << format_bytes(Data, None, Data.size(), 1, 0, true) << ")\n";
  }
}

} // namespace llvm

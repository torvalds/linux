#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"

using namespace llvm;
using namespace llvm::codeview;

static void writeRecordPrefix(BinaryStreamWriter &Writer, TypeLeafKind Kind) {
  RecordPrefix Prefix;
  Prefix.RecordKind = Kind;
  Prefix.RecordLen = 0;
  cantFail(Writer.writeObject(Prefix));
}

static void addPadding(BinaryStreamWriter &Writer) {
  uint32_t Align = Writer.getOffset() % 4;
  if (Align == 0)
    return;

  int PaddingBytes = 4 - Align;
  while (PaddingBytes > 0) {
    uint8_t Pad = static_cast<uint8_t>(LF_PAD0 + PaddingBytes);
    cantFail(Writer.writeInteger(Pad));
    --PaddingBytes;
  }
}

SimpleTypeSerializer::SimpleTypeSerializer() : ScratchBuffer(MaxRecordLength) {}

SimpleTypeSerializer::~SimpleTypeSerializer() {}

template <typename T>
ArrayRef<uint8_t> SimpleTypeSerializer::serialize(T &Record) {
  BinaryStreamWriter Writer(ScratchBuffer, support::little);
  TypeRecordMapping Mapping(Writer);

  CVType CVT;
  CVT.Type = static_cast<TypeLeafKind>(Record.getKind());

  writeRecordPrefix(Writer, CVT.Type);

  cantFail(Mapping.visitTypeBegin(CVT));
  cantFail(Mapping.visitKnownRecord(CVT, Record));
  cantFail(Mapping.visitTypeEnd(CVT));

  addPadding(Writer);

  RecordPrefix *Prefix = reinterpret_cast<RecordPrefix *>(ScratchBuffer.data());

  Prefix->RecordKind = CVT.kind();
  Prefix->RecordLen = Writer.getOffset() - sizeof(uint16_t);

  return {ScratchBuffer.data(), Writer.getOffset()};
}

// Explicitly instantiate the member function for each known type so that we can
// implement this in the cpp file.
#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  template ArrayRef<uint8_t> llvm::codeview::SimpleTypeSerializer::serialize(  \
      Name##Record &Record);
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"

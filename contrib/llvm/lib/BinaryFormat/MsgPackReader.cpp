//===- MsgPackReader.cpp - Simple MsgPack reader ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
///  \file
///  This file implements a MessagePack reader.
///
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/MsgPackReader.h"
#include "llvm/BinaryFormat/MsgPack.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support;
using namespace msgpack;

Reader::Reader(MemoryBufferRef InputBuffer)
    : InputBuffer(InputBuffer), Current(InputBuffer.getBufferStart()),
      End(InputBuffer.getBufferEnd()) {}

Reader::Reader(StringRef Input) : Reader({Input, "MsgPack"}) {}

Expected<bool> Reader::read(Object &Obj) {
  if (Current == End)
    return false;

  uint8_t FB = static_cast<uint8_t>(*Current++);

  switch (FB) {
  case FirstByte::Nil:
    Obj.Kind = Type::Nil;
    return true;
  case FirstByte::True:
    Obj.Kind = Type::Boolean;
    Obj.Bool = true;
    return true;
  case FirstByte::False:
    Obj.Kind = Type::Boolean;
    Obj.Bool = false;
    return true;
  case FirstByte::Int8:
    Obj.Kind = Type::Int;
    return readInt<int8_t>(Obj);
  case FirstByte::Int16:
    Obj.Kind = Type::Int;
    return readInt<int16_t>(Obj);
  case FirstByte::Int32:
    Obj.Kind = Type::Int;
    return readInt<int32_t>(Obj);
  case FirstByte::Int64:
    Obj.Kind = Type::Int;
    return readInt<int64_t>(Obj);
  case FirstByte::UInt8:
    Obj.Kind = Type::UInt;
    return readUInt<uint8_t>(Obj);
  case FirstByte::UInt16:
    Obj.Kind = Type::UInt;
    return readUInt<uint16_t>(Obj);
  case FirstByte::UInt32:
    Obj.Kind = Type::UInt;
    return readUInt<uint32_t>(Obj);
  case FirstByte::UInt64:
    Obj.Kind = Type::UInt;
    return readUInt<uint64_t>(Obj);
  case FirstByte::Float32:
    Obj.Kind = Type::Float;
    if (sizeof(float) > remainingSpace())
      return make_error<StringError>(
          "Invalid Float32 with insufficient payload",
          std::make_error_code(std::errc::invalid_argument));
    Obj.Float = BitsToFloat(endian::read<uint32_t, Endianness>(Current));
    Current += sizeof(float);
    return true;
  case FirstByte::Float64:
    Obj.Kind = Type::Float;
    if (sizeof(double) > remainingSpace())
      return make_error<StringError>(
          "Invalid Float64 with insufficient payload",
          std::make_error_code(std::errc::invalid_argument));
    Obj.Float = BitsToDouble(endian::read<uint64_t, Endianness>(Current));
    Current += sizeof(double);
    return true;
  case FirstByte::Str8:
    Obj.Kind = Type::String;
    return readRaw<uint8_t>(Obj);
  case FirstByte::Str16:
    Obj.Kind = Type::String;
    return readRaw<uint16_t>(Obj);
  case FirstByte::Str32:
    Obj.Kind = Type::String;
    return readRaw<uint32_t>(Obj);
  case FirstByte::Bin8:
    Obj.Kind = Type::Binary;
    return readRaw<uint8_t>(Obj);
  case FirstByte::Bin16:
    Obj.Kind = Type::Binary;
    return readRaw<uint16_t>(Obj);
  case FirstByte::Bin32:
    Obj.Kind = Type::Binary;
    return readRaw<uint32_t>(Obj);
  case FirstByte::Array16:
    Obj.Kind = Type::Array;
    return readLength<uint16_t>(Obj);
  case FirstByte::Array32:
    Obj.Kind = Type::Array;
    return readLength<uint32_t>(Obj);
  case FirstByte::Map16:
    Obj.Kind = Type::Map;
    return readLength<uint16_t>(Obj);
  case FirstByte::Map32:
    Obj.Kind = Type::Map;
    return readLength<uint32_t>(Obj);
  case FirstByte::FixExt1:
    Obj.Kind = Type::Extension;
    return createExt(Obj, FixLen::Ext1);
  case FirstByte::FixExt2:
    Obj.Kind = Type::Extension;
    return createExt(Obj, FixLen::Ext2);
  case FirstByte::FixExt4:
    Obj.Kind = Type::Extension;
    return createExt(Obj, FixLen::Ext4);
  case FirstByte::FixExt8:
    Obj.Kind = Type::Extension;
    return createExt(Obj, FixLen::Ext8);
  case FirstByte::FixExt16:
    Obj.Kind = Type::Extension;
    return createExt(Obj, FixLen::Ext16);
  case FirstByte::Ext8:
    Obj.Kind = Type::Extension;
    return readExt<uint8_t>(Obj);
  case FirstByte::Ext16:
    Obj.Kind = Type::Extension;
    return readExt<uint16_t>(Obj);
  case FirstByte::Ext32:
    Obj.Kind = Type::Extension;
    return readExt<uint32_t>(Obj);
  }

  if ((FB & FixBitsMask::NegativeInt) == FixBits::NegativeInt) {
    Obj.Kind = Type::Int;
    int8_t I;
    static_assert(sizeof(I) == sizeof(FB), "Unexpected type sizes");
    memcpy(&I, &FB, sizeof(FB));
    Obj.Int = I;
    return true;
  }

  if ((FB & FixBitsMask::PositiveInt) == FixBits::PositiveInt) {
    Obj.Kind = Type::UInt;
    Obj.UInt = FB;
    return true;
  }

  if ((FB & FixBitsMask::String) == FixBits::String) {
    Obj.Kind = Type::String;
    uint8_t Size = FB & ~FixBitsMask::String;
    return createRaw(Obj, Size);
  }

  if ((FB & FixBitsMask::Array) == FixBits::Array) {
    Obj.Kind = Type::Array;
    Obj.Length = FB & ~FixBitsMask::Array;
    return true;
  }

  if ((FB & FixBitsMask::Map) == FixBits::Map) {
    Obj.Kind = Type::Map;
    Obj.Length = FB & ~FixBitsMask::Map;
    return true;
  }

  return make_error<StringError>(
      "Invalid first byte", std::make_error_code(std::errc::invalid_argument));
}

template <class T> Expected<bool> Reader::readRaw(Object &Obj) {
  if (sizeof(T) > remainingSpace())
    return make_error<StringError>(
        "Invalid Raw with insufficient payload",
        std::make_error_code(std::errc::invalid_argument));
  T Size = endian::read<T, Endianness>(Current);
  Current += sizeof(T);
  return createRaw(Obj, Size);
}

template <class T> Expected<bool> Reader::readInt(Object &Obj) {
  if (sizeof(T) > remainingSpace())
    return make_error<StringError>(
        "Invalid Int with insufficient payload",
        std::make_error_code(std::errc::invalid_argument));
  Obj.Int = static_cast<int64_t>(endian::read<T, Endianness>(Current));
  Current += sizeof(T);
  return true;
}

template <class T> Expected<bool> Reader::readUInt(Object &Obj) {
  if (sizeof(T) > remainingSpace())
    return make_error<StringError>(
        "Invalid Int with insufficient payload",
        std::make_error_code(std::errc::invalid_argument));
  Obj.UInt = static_cast<uint64_t>(endian::read<T, Endianness>(Current));
  Current += sizeof(T);
  return true;
}

template <class T> Expected<bool> Reader::readLength(Object &Obj) {
  if (sizeof(T) > remainingSpace())
    return make_error<StringError>(
        "Invalid Map/Array with invalid length",
        std::make_error_code(std::errc::invalid_argument));
  Obj.Length = static_cast<size_t>(endian::read<T, Endianness>(Current));
  Current += sizeof(T);
  return true;
}

template <class T> Expected<bool> Reader::readExt(Object &Obj) {
  if (sizeof(T) > remainingSpace())
    return make_error<StringError>(
        "Invalid Ext with invalid length",
        std::make_error_code(std::errc::invalid_argument));
  T Size = endian::read<T, Endianness>(Current);
  Current += sizeof(T);
  return createExt(Obj, Size);
}

Expected<bool> Reader::createRaw(Object &Obj, uint32_t Size) {
  if (Size > remainingSpace())
    return make_error<StringError>(
        "Invalid Raw with insufficient payload",
        std::make_error_code(std::errc::invalid_argument));
  Obj.Raw = StringRef(Current, Size);
  Current += Size;
  return true;
}

Expected<bool> Reader::createExt(Object &Obj, uint32_t Size) {
  if (Current == End)
    return make_error<StringError>(
        "Invalid Ext with no type",
        std::make_error_code(std::errc::invalid_argument));
  Obj.Extension.Type = *Current++;
  if (Size > remainingSpace())
    return make_error<StringError>(
        "Invalid Ext with insufficient payload",
        std::make_error_code(std::errc::invalid_argument));
  Obj.Extension.Bytes = StringRef(Current, Size);
  Current += Size;
  return true;
}

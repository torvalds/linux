//===--- LayoutOverrideSource.cpp --Override Record Layouts ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "clang/Frontend/LayoutOverrideSource.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/CharInfo.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <string>

using namespace clang;

/// Parse a simple identifier.
static std::string parseName(StringRef S) {
  if (S.empty() || !isIdentifierHead(S[0]))
    return "";

  unsigned Offset = 1;
  while (Offset < S.size() && isIdentifierBody(S[Offset]))
    ++Offset;

  return S.substr(0, Offset).str();
}

LayoutOverrideSource::LayoutOverrideSource(StringRef Filename) {
  std::ifstream Input(Filename.str().c_str());
  if (!Input.is_open())
    return;

  // Parse the output of -fdump-record-layouts.
  std::string CurrentType;
  Layout CurrentLayout;
  bool ExpectingType = false;

  while (Input.good()) {
    std::string Line;
    getline(Input, Line);

    StringRef LineStr(Line);

    // Determine whether the following line will start a
    if (LineStr.find("*** Dumping AST Record Layout") != StringRef::npos)  {
      // Flush the last type/layout, if there is one.
      if (!CurrentType.empty())
        Layouts[CurrentType] = CurrentLayout;
      CurrentLayout = Layout();

      ExpectingType = true;
      continue;
    }

    // If we're expecting a type, grab it.
    if (ExpectingType) {
      ExpectingType = false;

      StringRef::size_type Pos;
      if ((Pos = LineStr.find("struct ")) != StringRef::npos)
        LineStr = LineStr.substr(Pos + strlen("struct "));
      else if ((Pos = LineStr.find("class ")) != StringRef::npos)
        LineStr = LineStr.substr(Pos + strlen("class "));
      else if ((Pos = LineStr.find("union ")) != StringRef::npos)
        LineStr = LineStr.substr(Pos + strlen("union "));
      else
        continue;

      // Find the name of the type.
      CurrentType = parseName(LineStr);
      CurrentLayout = Layout();
      continue;
    }

    // Check for the size of the type.
    StringRef::size_type Pos = LineStr.find(" Size:");
    if (Pos != StringRef::npos) {
      // Skip past the " Size:" prefix.
      LineStr = LineStr.substr(Pos + strlen(" Size:"));

      unsigned long long Size = 0;
      (void)LineStr.getAsInteger(10, Size);
      CurrentLayout.Size = Size;
      continue;
    }

    // Check for the alignment of the type.
    Pos = LineStr.find("Alignment:");
    if (Pos != StringRef::npos) {
      // Skip past the "Alignment:" prefix.
      LineStr = LineStr.substr(Pos + strlen("Alignment:"));

      unsigned long long Alignment = 0;
      (void)LineStr.getAsInteger(10, Alignment);
      CurrentLayout.Align = Alignment;
      continue;
    }

    // Check for the size/alignment of the type.
    Pos = LineStr.find("sizeof=");
    if (Pos != StringRef::npos) {
      /* Skip past the sizeof= prefix. */
      LineStr = LineStr.substr(Pos + strlen("sizeof="));

      // Parse size.
      unsigned long long Size = 0;
      (void)LineStr.getAsInteger(10, Size);
      CurrentLayout.Size = Size;

      Pos = LineStr.find("align=");
      if (Pos != StringRef::npos) {
        /* Skip past the align= prefix. */
        LineStr = LineStr.substr(Pos + strlen("align="));

        // Parse alignment.
        unsigned long long Alignment = 0;
        (void)LineStr.getAsInteger(10, Alignment);
        CurrentLayout.Align = Alignment;
      }

      continue;
    }

    // Check for the field offsets of the type.
    Pos = LineStr.find("FieldOffsets: [");
    if (Pos == StringRef::npos)
      continue;

    LineStr = LineStr.substr(Pos + strlen("FieldOffsets: ["));
    while (!LineStr.empty() && isDigit(LineStr[0])) {
      // Parse this offset.
      unsigned Idx = 1;
      while (Idx < LineStr.size() && isDigit(LineStr[Idx]))
        ++Idx;

      unsigned long long Offset = 0;
      (void)LineStr.substr(0, Idx).getAsInteger(10, Offset);

      CurrentLayout.FieldOffsets.push_back(Offset);

      // Skip over this offset, the following comma, and any spaces.
      LineStr = LineStr.substr(Idx + 1);
      while (!LineStr.empty() && isWhitespace(LineStr[0]))
        LineStr = LineStr.substr(1);
    }
  }

  // Flush the last type/layout, if there is one.
  if (!CurrentType.empty())
    Layouts[CurrentType] = CurrentLayout;
}

bool
LayoutOverrideSource::layoutRecordType(const RecordDecl *Record,
  uint64_t &Size, uint64_t &Alignment,
  llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
  llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
  llvm::DenseMap<const CXXRecordDecl *, CharUnits> &VirtualBaseOffsets)
{
  // We can't override unnamed declarations.
  if (!Record->getIdentifier())
    return false;

  // Check whether we have a layout for this record.
  llvm::StringMap<Layout>::iterator Known = Layouts.find(Record->getName());
  if (Known == Layouts.end())
    return false;

  // Provide field layouts.
  unsigned NumFields = 0;
  for (RecordDecl::field_iterator F = Record->field_begin(),
                               FEnd = Record->field_end();
       F != FEnd; ++F, ++NumFields) {
    if (NumFields >= Known->second.FieldOffsets.size())
      continue;

    FieldOffsets[*F] = Known->second.FieldOffsets[NumFields];
  }

  // Wrong number of fields.
  if (NumFields != Known->second.FieldOffsets.size())
    return false;

  Size = Known->second.Size;
  Alignment = Known->second.Align;
  return true;
}

LLVM_DUMP_METHOD void LayoutOverrideSource::dump() {
  raw_ostream &OS = llvm::errs();
  for (llvm::StringMap<Layout>::iterator L = Layouts.begin(),
                                      LEnd = Layouts.end();
       L != LEnd; ++L) {
    OS << "Type: blah " << L->first() << '\n';
    OS << "  Size:" << L->second.Size << '\n';
    OS << "  Alignment:" << L->second.Align << '\n';
    OS << "  FieldOffsets: [";
    for (unsigned I = 0, N = L->second.FieldOffsets.size(); I != N; ++I) {
      if (I)
        OS << ", ";
      OS << L->second.FieldOffsets[I];
    }
    OS << "]\n";
  }
}


//===--- IncludeStyle.cpp - Style of C++ #include directives -----*- C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Inclusions/IncludeStyle.h"

using clang::tooling::IncludeStyle;

namespace llvm {
namespace yaml {

void MappingTraits<IncludeStyle::IncludeCategory>::mapping(
    IO &IO, IncludeStyle::IncludeCategory &Category) {
  IO.mapOptional("Regex", Category.Regex);
  IO.mapOptional("Priority", Category.Priority);
}

void ScalarEnumerationTraits<IncludeStyle::IncludeBlocksStyle>::enumeration(
    IO &IO, IncludeStyle::IncludeBlocksStyle &Value) {
  IO.enumCase(Value, "Preserve", IncludeStyle::IBS_Preserve);
  IO.enumCase(Value, "Merge", IncludeStyle::IBS_Merge);
  IO.enumCase(Value, "Regroup", IncludeStyle::IBS_Regroup);
}

} // namespace yaml
} // namespace llvm

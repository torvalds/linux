//===--- DatatCollection.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares helper methods for collecting data from AST nodes.
///
/// To collect data from Stmt nodes, subclass ConstStmtVisitor and include
/// StmtDataCollectors.inc after defining the macros that you need. This
/// provides data collection implementations for most Stmt kinds. Note
/// that the code requires some conditions to be met:
///
///   - There must be a method addData(const T &Data) that accepts strings,
///     integral types as well as QualType. All data is forwarded using
///     to this method.
///   - The ASTContext of the Stmt must be accessible by the name Context.
///
/// It is also possible to override individual visit methods. Have a look at
/// the DataCollector in lib/Analysis/CloneDetection.cpp for a usage example.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DATACOLLECTION_H
#define LLVM_CLANG_AST_DATACOLLECTION_H

#include "clang/AST/ASTContext.h"

namespace clang {
namespace data_collection {

/// Returns a string that represents all macro expansions that expanded into the
/// given SourceLocation.
///
/// If 'getMacroStack(A) == getMacroStack(B)' is true, then the SourceLocations
/// A and B are expanded from the same macros in the same order.
std::string getMacroStack(SourceLocation Loc, ASTContext &Context);

/// Utility functions for implementing addData() for a consumer that has a
/// method update(StringRef)
template <class T>
void addDataToConsumer(T &DataConsumer, llvm::StringRef Str) {
  DataConsumer.update(Str);
}

template <class T> void addDataToConsumer(T &DataConsumer, const QualType &QT) {
  addDataToConsumer(DataConsumer, QT.getAsString());
}

template <class T, class Type>
typename std::enable_if<
    std::is_integral<Type>::value || std::is_enum<Type>::value ||
    std::is_convertible<Type, size_t>::value // for llvm::hash_code
    >::type
addDataToConsumer(T &DataConsumer, Type Data) {
  DataConsumer.update(StringRef(reinterpret_cast<char *>(&Data), sizeof(Data)));
}

} // end namespace data_collection
} // end namespace clang

#endif // LLVM_CLANG_AST_DATACOLLECTION_H

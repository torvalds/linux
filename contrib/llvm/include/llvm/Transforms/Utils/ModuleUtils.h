//===-- ModuleUtils.h - Functions to manipulate Modules ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform manipulations on Modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_MODULEUTILS_H
#define LLVM_TRANSFORMS_UTILS_MODULEUTILS_H

#include "llvm/ADT/StringRef.h"
#include <utility> // for std::pair

namespace llvm {

template <typename T> class ArrayRef;
class Module;
class Function;
class GlobalValue;
class GlobalVariable;
class Constant;
class StringRef;
class Value;
class Type;

/// Append F to the list of global ctors of module M with the given Priority.
/// This wraps the function in the appropriate structure and stores it along
/// side other global constructors. For details see
/// http://llvm.org/docs/LangRef.html#intg_global_ctors
void appendToGlobalCtors(Module &M, Function *F, int Priority,
                         Constant *Data = nullptr);

/// Same as appendToGlobalCtors(), but for global dtors.
void appendToGlobalDtors(Module &M, Function *F, int Priority,
                         Constant *Data = nullptr);

// Validate the result of Module::getOrInsertFunction called for an interface
// function of given sanitizer. If the instrumented module defines a function
// with the same name, their prototypes must match, otherwise
// getOrInsertFunction returns a bitcast.
Function *checkSanitizerInterfaceFunction(Constant *FuncOrBitcast);

Function *declareSanitizerInitFunction(Module &M, StringRef InitName,
                                       ArrayRef<Type *> InitArgTypes);

/// Creates sanitizer constructor function, and calls sanitizer's init
/// function from it.
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
std::pair<Function *, Function *> createSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    StringRef VersionCheckName = StringRef());

/// Creates sanitizer constructor function lazily. If a constructor and init
/// function already exist, this function returns it. Otherwise it calls \c
/// createSanitizerCtorAndInitFunctions. The FunctionsCreatedCallback is invoked
/// in that case, passing the new Ctor and Init function.
///
/// \return Returns pair of pointers to constructor, and init functions
/// respectively.
std::pair<Function *, Function *> getOrCreateSanitizerCtorAndInitFunctions(
    Module &M, StringRef CtorName, StringRef InitName,
    ArrayRef<Type *> InitArgTypes, ArrayRef<Value *> InitArgs,
    function_ref<void(Function *, Function *)> FunctionsCreatedCallback,
    StringRef VersionCheckName = StringRef());

// Creates and returns a sanitizer init function without argument if it doesn't
// exist, and adds it to the global constructors list. Otherwise it returns the
// existing function.
Function *getOrCreateInitFunction(Module &M, StringRef Name);

/// Rename all the anon globals in the module using a hash computed from
/// the list of public globals in the module.
bool nameUnamedGlobals(Module &M);

/// Adds global values to the llvm.used list.
void appendToUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Adds global values to the llvm.compiler.used list.
void appendToCompilerUsed(Module &M, ArrayRef<GlobalValue *> Values);

/// Filter out potentially dead comdat functions where other entries keep the
/// entire comdat group alive.
///
/// This is designed for cases where functions appear to become dead but remain
/// alive due to other live entries in their comdat group.
///
/// The \p DeadComdatFunctions container should only have pointers to
/// `Function`s which are members of a comdat group and are believed to be
/// dead.
///
/// After this routine finishes, the only remaining `Function`s in \p
/// DeadComdatFunctions are those where every member of the comdat is listed
/// and thus removing them is safe (provided *all* are removed).
void filterDeadComdatFunctions(
    Module &M, SmallVectorImpl<Function *> &DeadComdatFunctions);

/// Produce a unique identifier for this module by taking the MD5 sum of
/// the names of the module's strong external symbols that are not comdat
/// members.
///
/// This identifier is normally guaranteed to be unique, or the program would
/// fail to link due to multiply defined symbols.
///
/// If the module has no strong external symbols (such a module may still have a
/// semantic effect if it performs global initialization), we cannot produce a
/// unique identifier for this module, so we return the empty string.
std::string getUniqueModuleId(Module *M);

} // End llvm namespace

#endif //  LLVM_TRANSFORMS_UTILS_MODULEUTILS_H

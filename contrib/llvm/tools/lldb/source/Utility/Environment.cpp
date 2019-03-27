//===-- Environment.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Environment.h"

using namespace lldb_private;

char *Environment::Envp::make_entry(llvm::StringRef Key,
                                    llvm::StringRef Value) {
  const size_t size = Key.size() + 1 /*=*/ + Value.size() + 1 /*\0*/;
  char *Result = reinterpret_cast<char *>(
      Allocator.Allocate(sizeof(char) * size, alignof(char)));
  char *Next = Result;

  Next = std::copy(Key.begin(), Key.end(), Next);
  *Next++ = '=';
  Next = std::copy(Value.begin(), Value.end(), Next);
  *Next++ = '\0';

  return Result;
}

Environment::Envp::Envp(const Environment &Env) {
  Data = reinterpret_cast<char **>(
      Allocator.Allocate(sizeof(char *) * (Env.size() + 1), alignof(char *)));
  char **Next = Data;
  for (const auto &KV : Env)
    *Next++ = make_entry(KV.first(), KV.second);
  *Next++ = nullptr;
}

Environment::Environment(const char *const *Env) {
  if (!Env)
    return;
  while (*Env)
    insert(*Env++);
}

void Environment::insert(const_iterator first, const_iterator last) {
  while (first != last) {
    try_emplace(first->first(), first->second);
    ++first;
  }
}

//===-- stats_client.cc ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Sanitizer statistics gathering. Manages statistics for a module (executable
// or DSO) and registers statistics with the process.
//
// This is linked into each individual modle and cannot directly use functions
// declared in sanitizer_common.
//
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <stdint.h>
#include <stdio.h>

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "stats/stats.h"

using namespace __sanitizer;

namespace {

void *LookupSymbolFromMain(const char *name) {
#ifdef _WIN32
  return reinterpret_cast<void *>(GetProcAddress(GetModuleHandle(0), name));
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

StatModule *list;

struct RegisterSanStats {
  unsigned module_id;

  RegisterSanStats() {
    typedef unsigned (*reg_func_t)(StatModule **);
    reg_func_t reg_func = reinterpret_cast<reg_func_t>(
        LookupSymbolFromMain("__sanitizer_stats_register"));
    if (reg_func)
      module_id = reg_func(&list);
  }

  ~RegisterSanStats() {
    typedef void (*unreg_func_t)(unsigned);
    unreg_func_t unreg_func = reinterpret_cast<unreg_func_t>(
        LookupSymbolFromMain("__sanitizer_stats_unregister"));
    if (unreg_func)
      unreg_func(module_id);
  }
} reg;

}

extern "C" void __sanitizer_stat_init(StatModule *mod) {
  mod->next = list;
  list = mod;
}

extern "C" void __sanitizer_stat_report(StatInfo *s) {
  s->addr = GET_CALLER_PC();
#if defined(_WIN64) && !defined(__clang__)
  uptr old_data = InterlockedIncrement64(reinterpret_cast<LONG64 *>(&s->data));
#elif defined(_WIN32) && !defined(__clang__)
  uptr old_data = InterlockedIncrement(&s->data);
#else
  uptr old_data = __sync_fetch_and_add(&s->data, 1);
#endif

  // Overflow check.
  if (CountFromData(old_data + 1) == 0)
    Trap();
}

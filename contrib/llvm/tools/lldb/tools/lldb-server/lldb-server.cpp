//===-- lldb-server.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerLLGS.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"

#include <stdio.h>
#include <stdlib.h>

static llvm::ManagedStatic<lldb_private::SystemLifetimeManager>
    g_debugger_lifetime;

static void display_usage(const char *progname) {
  fprintf(stderr, "Usage:\n"
                  "  %s v[ersion]\n"
                  "  %s g[dbserver] [options]\n"
                  "  %s p[latform] [options]\n"
                  "Invoke subcommand for additional help\n",
          progname, progname, progname);
  exit(0);
}

// Forward declarations of subcommand main methods.
int main_gdbserver(int argc, char *argv[]);
int main_platform(int argc, char *argv[]);

static void initialize() {
  if (auto e = g_debugger_lifetime->Initialize(
          llvm::make_unique<SystemInitializerLLGS>(), {}, nullptr))
    llvm::consumeError(std::move(e));
}

static void terminate() { g_debugger_lifetime->Terminate(); }

//----------------------------------------------------------------------
// main
//----------------------------------------------------------------------
int main(int argc, char *argv[]) {
  llvm::StringRef ToolName = argv[0];
  llvm::sys::PrintStackTraceOnErrorSignal(ToolName);
  llvm::PrettyStackTraceProgram X(argc, argv);

  int option_error = 0;
  const char *progname = argv[0];
  if (argc < 2) {
    display_usage(progname);
    exit(option_error);
  }

  switch (argv[1][0]) {
  case 'g':
    initialize();
    main_gdbserver(argc, argv);
    terminate();
    break;
  case 'p':
    initialize();
    main_platform(argc, argv);
    terminate();
    break;
  case 'v':
    fprintf(stderr, "%s\n", lldb_private::GetVersion());
    break;
  default:
    display_usage(progname);
    exit(option_error);
  }
}

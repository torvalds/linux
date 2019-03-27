//===-- lldb-gdbserver.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif


#include "Acceptor.h"
#include "LLDBServerUtilities.h"
#include "Plugins/Process/gdb-remote/GDBRemoteCommunicationServerLLGS.h"
#include "Plugins/Process/gdb-remote/ProcessGDBRemoteLog.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostGetOpt.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Utility/Status.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errno.h"

#if defined(__linux__)
#include "Plugins/Process/Linux/NativeProcessLinux.h"
#elif defined(__NetBSD__)
#include "Plugins/Process/NetBSD/NativeProcessNetBSD.h"
#endif

#ifndef LLGS_PROGRAM_NAME
#define LLGS_PROGRAM_NAME "lldb-server"
#endif

#ifndef LLGS_VERSION_STR
#define LLGS_VERSION_STR "local_build"
#endif

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::lldb_server;
using namespace lldb_private::process_gdb_remote;

namespace {
#if defined(__linux__)
typedef process_linux::NativeProcessLinux::Factory NativeProcessFactory;
#elif defined(__NetBSD__)
typedef process_netbsd::NativeProcessNetBSD::Factory NativeProcessFactory;
#else
// Dummy implementation to make sure the code compiles
class NativeProcessFactory : public NativeProcessProtocol::Factory {
public:
  llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
  Launch(ProcessLaunchInfo &launch_info,
         NativeProcessProtocol::NativeDelegate &delegate,
         MainLoop &mainloop) const override {
    llvm_unreachable("Not implemented");
  }
  llvm::Expected<std::unique_ptr<NativeProcessProtocol>>
  Attach(lldb::pid_t pid, NativeProcessProtocol::NativeDelegate &delegate,
         MainLoop &mainloop) const override {
    llvm_unreachable("Not implemented");
  }
};
#endif
}

//----------------------------------------------------------------------
// option descriptors for getopt_long_only()
//----------------------------------------------------------------------

static int g_debug = 0;
static int g_verbose = 0;

static struct option g_long_options[] = {
    {"debug", no_argument, &g_debug, 1},
    {"verbose", no_argument, &g_verbose, 1},
    {"log-file", required_argument, NULL, 'l'},
    {"log-channels", required_argument, NULL, 'c'},
    {"attach", required_argument, NULL, 'a'},
    {"named-pipe", required_argument, NULL, 'N'},
    {"pipe", required_argument, NULL, 'U'},
    {"native-regs", no_argument, NULL,
     'r'}, // Specify to use the native registers instead of the gdb defaults
           // for the architecture.  NOTE: this is a do-nothing arg as it's
           // behavior is default now.  FIXME remove call from lldb-platform.
    {"reverse-connect", no_argument, NULL,
     'R'}, // Specifies that llgs attaches to the client address:port rather
           // than llgs listening for a connection from address on port.
    {"setsid", no_argument, NULL,
     'S'}, // Call setsid() to make llgs run in its own session.
    {"fd", required_argument, NULL, 'F'},
    {NULL, 0, NULL, 0}};

//----------------------------------------------------------------------
// Watch for signals
//----------------------------------------------------------------------
static int g_sighup_received_count = 0;

#ifndef _WIN32
static void sighup_handler(MainLoopBase &mainloop) {
  ++g_sighup_received_count;

  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_PROCESS));
  if (log)
    log->Printf("lldb-server:%s swallowing SIGHUP (receive count=%d)",
                __FUNCTION__, g_sighup_received_count);

  if (g_sighup_received_count >= 2)
    mainloop.RequestTermination();
}
#endif // #ifndef _WIN32

static void display_usage(const char *progname, const char *subcommand) {
  fprintf(stderr, "Usage:\n  %s %s "
                  "[--log-file log-file-name] "
                  "[--log-channels log-channel-list] "
                  "[--setsid] "
                  "[--fd file-descriptor]"
                  "[--named-pipe named-pipe-path] "
                  "[--native-regs] "
                  "[--attach pid] "
                  "[[HOST]:PORT] "
                  "[-- PROGRAM ARG1 ARG2 ...]\n",
          progname, subcommand);
}

void handle_attach_to_pid(GDBRemoteCommunicationServerLLGS &gdb_server,
                          lldb::pid_t pid) {
  Status error = gdb_server.AttachToProcess(pid);
  if (error.Fail()) {
    fprintf(stderr, "error: failed to attach to pid %" PRIu64 ": %s\n", pid,
            error.AsCString());
    exit(1);
  }
}

void handle_attach_to_process_name(GDBRemoteCommunicationServerLLGS &gdb_server,
                                   const std::string &process_name) {
  // FIXME implement.
}

void handle_attach(GDBRemoteCommunicationServerLLGS &gdb_server,
                   const std::string &attach_target) {
  assert(!attach_target.empty() && "attach_target cannot be empty");

  // First check if the attach_target is convertible to a long. If so, we'll use
  // it as a pid.
  char *end_p = nullptr;
  const long int pid = strtol(attach_target.c_str(), &end_p, 10);

  // We'll call it a match if the entire argument is consumed.
  if (end_p &&
      static_cast<size_t>(end_p - attach_target.c_str()) ==
          attach_target.size())
    handle_attach_to_pid(gdb_server, static_cast<lldb::pid_t>(pid));
  else
    handle_attach_to_process_name(gdb_server, attach_target);
}

void handle_launch(GDBRemoteCommunicationServerLLGS &gdb_server, int argc,
                   const char *const argv[]) {
  ProcessLaunchInfo info;
  info.GetFlags().Set(eLaunchFlagStopAtEntry | eLaunchFlagDebug |
                      eLaunchFlagDisableASLR);
  info.SetArguments(const_cast<const char **>(argv), true);

  llvm::SmallString<64> cwd;
  if (std::error_code ec = llvm::sys::fs::current_path(cwd)) {
    llvm::errs() << "Error getting current directory: " << ec.message() << "\n";
    exit(1);
  }
  FileSpec cwd_spec(cwd);
  FileSystem::Instance().Resolve(cwd_spec);
  info.SetWorkingDirectory(cwd_spec);
  info.GetEnvironment() = Host::GetEnvironment();

  gdb_server.SetLaunchInfo(info);

  Status error = gdb_server.LaunchProcess();
  if (error.Fail()) {
    llvm::errs() << llvm::formatv("error: failed to launch '{0}': {1}\n",
                                  argv[0], error);
    exit(1);
  }
}

Status writeSocketIdToPipe(Pipe &port_pipe, const std::string &socket_id) {
  size_t bytes_written = 0;
  // Write the port number as a C string with the NULL terminator.
  return port_pipe.Write(socket_id.c_str(), socket_id.size() + 1,
                         bytes_written);
}

Status writeSocketIdToPipe(const char *const named_pipe_path,
                           const std::string &socket_id) {
  Pipe port_name_pipe;
  // Wait for 10 seconds for pipe to be opened.
  auto error = port_name_pipe.OpenAsWriterWithTimeout(named_pipe_path, false,
                                                      std::chrono::seconds{10});
  if (error.Fail())
    return error;
  return writeSocketIdToPipe(port_name_pipe, socket_id);
}

Status writeSocketIdToPipe(lldb::pipe_t unnamed_pipe,
                           const std::string &socket_id) {
  Pipe port_pipe{LLDB_INVALID_PIPE, unnamed_pipe};
  return writeSocketIdToPipe(port_pipe, socket_id);
}

void ConnectToRemote(MainLoop &mainloop,
                     GDBRemoteCommunicationServerLLGS &gdb_server,
                     bool reverse_connect, const char *const host_and_port,
                     const char *const progname, const char *const subcommand,
                     const char *const named_pipe_path, pipe_t unnamed_pipe,
                     int connection_fd) {
  Status error;

  std::unique_ptr<Connection> connection_up;
  if (connection_fd != -1) {
    // Build the connection string.
    char connection_url[512];
    snprintf(connection_url, sizeof(connection_url), "fd://%d", connection_fd);

    // Create the connection.
#if !defined LLDB_DISABLE_POSIX && !defined _WIN32
    ::fcntl(connection_fd, F_SETFD, FD_CLOEXEC);
#endif
    connection_up.reset(new ConnectionFileDescriptor);
    auto connection_result = connection_up->Connect(connection_url, &error);
    if (connection_result != eConnectionStatusSuccess) {
      fprintf(stderr, "error: failed to connect to client at '%s' "
                      "(connection status: %d)\n",
              connection_url, static_cast<int>(connection_result));
      exit(-1);
    }
    if (error.Fail()) {
      fprintf(stderr, "error: failed to connect to client at '%s': %s\n",
              connection_url, error.AsCString());
      exit(-1);
    }
  } else if (host_and_port && host_and_port[0]) {
    // Parse out host and port.
    std::string final_host_and_port;
    std::string connection_host;
    std::string connection_port;
    uint32_t connection_portno = 0;

    // If host_and_port starts with ':', default the host to be "localhost" and
    // expect the remainder to be the port.
    if (host_and_port[0] == ':')
      final_host_and_port.append("localhost");
    final_host_and_port.append(host_and_port);

    const std::string::size_type colon_pos = final_host_and_port.find(':');
    if (colon_pos != std::string::npos) {
      connection_host = final_host_and_port.substr(0, colon_pos);
      connection_port = final_host_and_port.substr(colon_pos + 1);
      connection_portno = StringConvert::ToUInt32(connection_port.c_str(), 0);
    }


    if (reverse_connect) {
      // llgs will connect to the gdb-remote client.

      // Ensure we have a port number for the connection.
      if (connection_portno == 0) {
        fprintf(stderr, "error: port number must be specified on when using "
                        "reverse connect\n");
        exit(1);
      }

      // Build the connection string.
      char connection_url[512];
      snprintf(connection_url, sizeof(connection_url), "connect://%s",
               final_host_and_port.c_str());

      // Create the connection.
      connection_up.reset(new ConnectionFileDescriptor);
      auto connection_result = connection_up->Connect(connection_url, &error);
      if (connection_result != eConnectionStatusSuccess) {
        fprintf(stderr, "error: failed to connect to client at '%s' "
                        "(connection status: %d)\n",
                connection_url, static_cast<int>(connection_result));
        exit(-1);
      }
      if (error.Fail()) {
        fprintf(stderr, "error: failed to connect to client at '%s': %s\n",
                connection_url, error.AsCString());
        exit(-1);
      }
    } else {
      std::unique_ptr<Acceptor> acceptor_up(
          Acceptor::Create(final_host_and_port, false, error));
      if (error.Fail()) {
        fprintf(stderr, "failed to create acceptor: %s\n", error.AsCString());
        exit(1);
      }
      error = acceptor_up->Listen(1);
      if (error.Fail()) {
        fprintf(stderr, "failed to listen: %s\n", error.AsCString());
        exit(1);
      }
      const std::string socket_id = acceptor_up->GetLocalSocketId();
      if (!socket_id.empty()) {
        // If we have a named pipe to write the socket id back to, do that now.
        if (named_pipe_path && named_pipe_path[0]) {
          error = writeSocketIdToPipe(named_pipe_path, socket_id);
          if (error.Fail())
            fprintf(stderr, "failed to write to the named pipe \'%s\': %s\n",
                    named_pipe_path, error.AsCString());
        }
        // If we have an unnamed pipe to write the socket id back to, do that
        // now.
        else if (unnamed_pipe != LLDB_INVALID_PIPE) {
          error = writeSocketIdToPipe(unnamed_pipe, socket_id);
          if (error.Fail())
            fprintf(stderr, "failed to write to the unnamed pipe: %s\n",
                    error.AsCString());
        }
      } else {
        fprintf(stderr,
                "unable to get the socket id for the listening connection\n");
      }

      Connection *conn = nullptr;
      error = acceptor_up->Accept(false, conn);
      if (error.Fail()) {
        printf("failed to accept new connection: %s\n", error.AsCString());
        exit(1);
      }
      connection_up.reset(conn);
    }
  }
  error = gdb_server.InitializeConnection(std::move(connection_up));
  if (error.Fail()) {
    fprintf(stderr, "Failed to initialize connection: %s\n",
            error.AsCString());
    exit(-1);
  }
  printf("Connection established.\n");
}

//----------------------------------------------------------------------
// main
//----------------------------------------------------------------------
int main_gdbserver(int argc, char *argv[]) {
  Status error;
  MainLoop mainloop;
#ifndef _WIN32
  // Setup signal handlers first thing.
  signal(SIGPIPE, SIG_IGN);
  MainLoop::SignalHandleUP sighup_handle =
      mainloop.RegisterSignal(SIGHUP, sighup_handler, error);
#endif

  const char *progname = argv[0];
  const char *subcommand = argv[1];
  argc--;
  argv++;
  int long_option_index = 0;
  int ch;
  std::string attach_target;
  std::string named_pipe_path;
  std::string log_file;
  StringRef
      log_channels; // e.g. "lldb process threads:gdb-remote default:linux all"
  lldb::pipe_t unnamed_pipe = LLDB_INVALID_PIPE;
  bool reverse_connect = false;
  int connection_fd = -1;

  // ProcessLaunchInfo launch_info;
  ProcessAttachInfo attach_info;

  bool show_usage = false;
  int option_error = 0;
#if __GLIBC__
  optind = 0;
#else
  optreset = 1;
  optind = 1;
#endif

  std::string short_options(OptionParser::GetShortOptionString(g_long_options));

  while ((ch = getopt_long_only(argc, argv, short_options.c_str(),
                                g_long_options, &long_option_index)) != -1) {
    switch (ch) {
    case 0: // Any optional that auto set themselves will return 0
      break;

    case 'l': // Set Log File
      if (optarg && optarg[0])
        log_file.assign(optarg);
      break;

    case 'c': // Log Channels
      if (optarg && optarg[0])
        log_channels = StringRef(optarg);
      break;

    case 'N': // named pipe
      if (optarg && optarg[0])
        named_pipe_path = optarg;
      break;

    case 'U': // unnamed pipe
      if (optarg && optarg[0])
        unnamed_pipe = (pipe_t)StringConvert::ToUInt64(optarg, -1);
      break;

    case 'r':
      // Do nothing, native regs is the default these days
      break;

    case 'R':
      reverse_connect = true;
      break;

    case 'F':
      connection_fd = StringConvert::ToUInt32(optarg, -1);
      break;

#ifndef _WIN32
    case 'S':
      // Put llgs into a new session. Terminals group processes
      // into sessions and when a special terminal key sequences
      // (like control+c) are typed they can cause signals to go out to
      // all processes in a session. Using this --setsid (-S) option
      // will cause debugserver to run in its own sessions and be free
      // from such issues.
      //
      // This is useful when llgs is spawned from a command
      // line application that uses llgs to do the debugging,
      // yet that application doesn't want llgs receiving the
      // signals sent to the session (i.e. dying when anyone hits ^C).
      {
        const ::pid_t new_sid = setsid();
        if (new_sid == -1) {
          llvm::errs() << llvm::formatv(
              "failed to set new session id for {0} ({1})\n", LLGS_PROGRAM_NAME,
              llvm::sys::StrError());
        }
      }
      break;
#endif

    case 'a': // attach {pid|process_name}
      if (optarg && optarg[0])
        attach_target = optarg;
      break;

    case 'h': /* fall-through is intentional */
    case '?':
      show_usage = true;
      break;
    }
  }

  if (show_usage || option_error) {
    display_usage(progname, subcommand);
    exit(option_error);
  }

  if (!LLDBServerUtilities::SetupLogging(
          log_file, log_channels,
          LLDB_LOG_OPTION_PREPEND_TIMESTAMP |
              LLDB_LOG_OPTION_PREPEND_FILE_FUNCTION))
    return -1;

  Log *log(lldb_private::GetLogIfAnyCategoriesSet(GDBR_LOG_PROCESS));
  if (log) {
    log->Printf("lldb-server launch");
    for (int i = 0; i < argc; i++) {
      log->Printf("argv[%i] = '%s'", i, argv[i]);
    }
  }

  // Skip any options we consumed with getopt_long_only.
  argc -= optind;
  argv += optind;

  if (argc == 0 && connection_fd == -1) {
    fputs("No arguments\n", stderr);
    display_usage(progname, subcommand);
    exit(255);
  }

  NativeProcessFactory factory;
  GDBRemoteCommunicationServerLLGS gdb_server(mainloop, factory);

  const char *const host_and_port = argv[0];
  argc -= 1;
  argv += 1;

  // Any arguments left over are for the program that we need to launch. If
  // there
  // are no arguments, then the GDB server will start up and wait for an 'A'
  // packet
  // to launch a program, or a vAttach packet to attach to an existing process,
  // unless
  // explicitly asked to attach with the --attach={pid|program_name} form.
  if (!attach_target.empty())
    handle_attach(gdb_server, attach_target);
  else if (argc > 0)
    handle_launch(gdb_server, argc, argv);

  // Print version info.
  printf("%s-%s", LLGS_PROGRAM_NAME, LLGS_VERSION_STR);

  ConnectToRemote(mainloop, gdb_server, reverse_connect, host_and_port,
                  progname, subcommand, named_pipe_path.c_str(), 
                  unnamed_pipe, connection_fd);

  if (!gdb_server.IsConnected()) {
    fprintf(stderr, "no connection information provided, unable to run\n");
    display_usage(progname, subcommand);
    return 1;
  }

  mainloop.Run();
  fprintf(stderr, "lldb-server exiting...\n");

  return 0;
}

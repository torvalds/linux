//===-- tsan_fd.h -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// This file handles synchronization via IO.
// People use IO for synchronization along the lines of:
//
// int X;
// int client_socket;  // initialized elsewhere
// int server_socket;  // initialized elsewhere
//
// Thread 1:
// X = 42;
// send(client_socket, ...);
//
// Thread 2:
// if (recv(server_socket, ...) > 0)
//   assert(X == 42);
//
// This file determines the scope of the file descriptor (pipe, socket,
// all local files, etc) and executes acquire and release operations on
// the scope as necessary.  Some scopes are very fine grained (e.g. pipe
// operations synchronize only with operations on the same pipe), while
// others are corse-grained (e.g. all operations on local files synchronize
// with each other).
//===----------------------------------------------------------------------===//
#ifndef TSAN_FD_H
#define TSAN_FD_H

#include "tsan_rtl.h"

namespace __tsan {

void FdInit();
void FdAcquire(ThreadState *thr, uptr pc, int fd);
void FdRelease(ThreadState *thr, uptr pc, int fd);
void FdAccess(ThreadState *thr, uptr pc, int fd);
void FdClose(ThreadState *thr, uptr pc, int fd, bool write = true);
void FdFileCreate(ThreadState *thr, uptr pc, int fd);
void FdDup(ThreadState *thr, uptr pc, int oldfd, int newfd, bool write);
void FdPipeCreate(ThreadState *thr, uptr pc, int rfd, int wfd);
void FdEventCreate(ThreadState *thr, uptr pc, int fd);
void FdSignalCreate(ThreadState *thr, uptr pc, int fd);
void FdInotifyCreate(ThreadState *thr, uptr pc, int fd);
void FdPollCreate(ThreadState *thr, uptr pc, int fd);
void FdSocketCreate(ThreadState *thr, uptr pc, int fd);
void FdSocketAccept(ThreadState *thr, uptr pc, int fd, int newfd);
void FdSocketConnecting(ThreadState *thr, uptr pc, int fd);
void FdSocketConnect(ThreadState *thr, uptr pc, int fd);
bool FdLocation(uptr addr, int *fd, int *tid, u32 *stack);
void FdOnFork(ThreadState *thr, uptr pc);

uptr File2addr(const char *path);
uptr Dir2addr(const char *path);

}  // namespace __tsan

#endif  // TSAN_INTERFACE_H

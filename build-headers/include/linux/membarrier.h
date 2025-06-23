#ifndef _LINUX_MEMBARRIER_H
#define _LINUX_MEMBARRIER_H

/*
 * linux/membarrier.h
 *
 * membarrier system call API
 *
 * Copyright (c) 2010, 2015 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * enum membarrier_cmd - membarrier system call command
 * @MEMBARRIER_CMD_QUERY:   Query the set of supported commands. It returns
 *                          a bitmask of valid commands.
 * @MEMBARRIER_CMD_GLOBAL:  Execute a memory barrier on all running threads.
 *                          Upon return from system call, the caller thread
 *                          is ensured that all running threads have passed
 *                          through a state where all memory accesses to
 *                          user-space addresses match program order between
 *                          entry to and return from the system call
 *                          (non-running threads are de facto in such a
 *                          state). This covers threads from all processes
 *                          running on the system. This command returns 0.
 * @MEMBARRIER_CMD_GLOBAL_EXPEDITED:
 *                          Execute a memory barrier on all running threads
 *                          of all processes which previously registered
 *                          with MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED.
 *                          Upon return from system call, the caller thread
 *                          is ensured that all running threads have passed
 *                          through a state where all memory accesses to
 *                          user-space addresses match program order between
 *                          entry to and return from the system call
 *                          (non-running threads are de facto in such a
 *                          state). This only covers threads from processes
 *                          which registered with
 *                          MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED.
 *                          This command returns 0. Given that
 *                          registration is about the intent to receive
 *                          the barriers, it is valid to invoke
 *                          MEMBARRIER_CMD_GLOBAL_EXPEDITED from a
 *                          non-registered process.
 * @MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED:
 *                          Register the process intent to receive
 *                          MEMBARRIER_CMD_GLOBAL_EXPEDITED memory
 *                          barriers. Always returns 0.
 * @MEMBARRIER_CMD_PRIVATE_EXPEDITED:
 *                          Execute a memory barrier on each running
 *                          thread belonging to the same process as the current
 *                          thread. Upon return from system call, the
 *                          caller thread is ensured that all its running
 *                          threads siblings have passed through a state
 *                          where all memory accesses to user-space
 *                          addresses match program order between entry
 *                          to and return from the system call
 *                          (non-running threads are de facto in such a
 *                          state). This only covers threads from the
 *                          same process as the caller thread. This
 *                          command returns 0 on success. The
 *                          "expedited" commands complete faster than
 *                          the non-expedited ones, they never block,
 *                          but have the downside of causing extra
 *                          overhead. A process needs to register its
 *                          intent to use the private expedited command
 *                          prior to using it, otherwise this command
 *                          returns -EPERM.
 * @MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED:
 *                          Register the process intent to use
 *                          MEMBARRIER_CMD_PRIVATE_EXPEDITED. Always
 *                          returns 0.
 * @MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE:
 *                          In addition to provide memory ordering
 *                          guarantees described in
 *                          MEMBARRIER_CMD_PRIVATE_EXPEDITED, ensure
 *                          the caller thread, upon return from system
 *                          call, that all its running threads siblings
 *                          have executed a core serializing
 *                          instruction. (architectures are required to
 *                          guarantee that non-running threads issue
 *                          core serializing instructions before they
 *                          resume user-space execution). This only
 *                          covers threads from the same process as the
 *                          caller thread. This command returns 0 on
 *                          success. The "expedited" commands complete
 *                          faster than the non-expedited ones, they
 *                          never block, but have the downside of
 *                          causing extra overhead. If this command is
 *                          not implemented by an architecture, -EINVAL
 *                          is returned. A process needs to register its
 *                          intent to use the private expedited sync
 *                          core command prior to using it, otherwise
 *                          this command returns -EPERM.
 * @MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE:
 *                          Register the process intent to use
 *                          MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE.
 *                          If this command is not implemented by an
 *                          architecture, -EINVAL is returned.
 *                          Returns 0 on success.
 * @MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ:
 *                          Ensure the caller thread, upon return from
 *                          system call, that all its running thread
 *                          siblings have any currently running rseq
 *                          critical sections restarted if @flags
 *                          parameter is 0; if @flags parameter is
 *                          MEMBARRIER_CMD_FLAG_CPU,
 *                          then this operation is performed only
 *                          on CPU indicated by @cpu_id. If this command is
 *                          not implemented by an architecture, -EINVAL
 *                          is returned. A process needs to register its
 *                          intent to use the private expedited rseq
 *                          command prior to using it, otherwise
 *                          this command returns -EPERM.
 * @MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ:
 *                          Register the process intent to use
 *                          MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ.
 *                          If this command is not implemented by an
 *                          architecture, -EINVAL is returned.
 *                          Returns 0 on success.
 * @MEMBARRIER_CMD_SHARED:
 *                          Alias to MEMBARRIER_CMD_GLOBAL. Provided for
 *                          header backward compatibility.
 *
 * Command to be passed to the membarrier system call. The commands need to
 * be a single bit each, except for MEMBARRIER_CMD_QUERY which is assigned to
 * the value 0.
 */
enum membarrier_cmd {
	MEMBARRIER_CMD_QUERY					= 0,
	MEMBARRIER_CMD_GLOBAL					= (1 << 0),
	MEMBARRIER_CMD_GLOBAL_EXPEDITED				= (1 << 1),
	MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED		= (1 << 2),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED			= (1 << 3),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED		= (1 << 4),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE		= (1 << 5),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE	= (1 << 6),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ			= (1 << 7),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ		= (1 << 8),

	/* Alias for header backward compatibility. */
	MEMBARRIER_CMD_SHARED			= MEMBARRIER_CMD_GLOBAL,
};

enum membarrier_cmd_flag {
	MEMBARRIER_CMD_FLAG_CPU		= (1 << 0),
};

#endif /* _LINUX_MEMBARRIER_H */

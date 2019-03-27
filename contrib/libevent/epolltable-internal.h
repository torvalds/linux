/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef EPOLLTABLE_INTERNAL_H_INCLUDED_
#define EPOLLTABLE_INTERNAL_H_INCLUDED_

/*
  Here are the values we're masking off to decide what operations to do.
  Note that since EV_READ|EV_WRITE.

  Note also that this table is a little sparse, since ADD+DEL is
  nonsensical ("xxx" in the list below.)

  Note also also that we are shifting old_events by only 5 bits, since
  EV_READ is 2 and EV_WRITE is 4.

  The table was auto-generated with a python script, according to this
  pseudocode:[*0]

      If either the read or the write change is add+del:
	 This is impossible; Set op==-1, events=0.
      Else, if either the read or the write change is add:
	 Set events to 0.
	 If the read change is add, or
	    (the read change is not del, and ev_read is in old_events):
	       Add EPOLLIN to events.
	 If the write change is add, or
	    (the write change is not del, and ev_write is in old_events):
	       Add EPOLLOUT to events.

	 If old_events is set:
	       Set op to EPOLL_CTL_MOD [*1,*2]
	Else:
	       Set op to EPOLL_CTL_ADD [*3]

      Else, if the read or the write change is del:
	 Set op to EPOLL_CTL_DEL.
	 If the read change is del:
	     If the write change is del:
		 Set events to EPOLLIN|EPOLLOUT
	     Else if ev_write is in old_events:
		 Set events to EPOLLOUT
		Set op to EPOLL_CTL_MOD
	     Else
		 Set events to EPOLLIN
	 Else:
	     {The write change is del.}
	    If ev_read is in old_events:
		 Set events to EPOLLIN
		Set op to EPOLL_CTL_MOD
	    Else:
		Set the events to EPOLLOUT

      Else:
	   There is no read or write change; set op to 0 and events to 0.

      The logic is a little tricky, since we had no events set on the fd before,
      we need to set op="ADD" and set events=the events we want to add.	 If we
      had any events set on the fd before, and we want any events to remain on
      the fd, we need to say op="MOD" and set events=the events we want to
      remain.  But if we want to delete the last event, we say op="DEL" and
      set events=(any non-null pointer).

  [*0] Actually, the Python script has gotten a bit more complicated, to
       support EPOLLRDHUP.

  [*1] This MOD is only a guess.  MOD might fail with ENOENT if the file was
       closed and a new file was opened with the same fd.  If so, we'll retry
       with ADD.

  [*2] We can't replace this with a no-op even if old_events is the same as
       the new events: if the file was closed and reopened, we need to retry
       with an ADD.  (We do a MOD in this case since "no change" is more
       common than "close and reopen", so we'll usually wind up doing 1
       syscalls instead of 2.)

  [*3] This ADD is only a guess.  There is a fun Linux kernel issue where if
       you have two fds for the same file (via dup) and you ADD one to an
       epfd, then close it, then re-create it with the same fd (via dup2 or an
       unlucky dup), then try to ADD it again, you'll get an EEXIST, since the
       struct epitem is not actually removed from the struct eventpoll until
       the file itself is closed.

  EV_CHANGE_ADD==1
  EV_CHANGE_DEL==2
  EV_READ      ==2
  EV_WRITE     ==4
  EV_CLOSED    ==0x80

  Bit 0: close change is add
  Bit 1: close change is del
  Bit 2: read change is add
  Bit 3: read change is del
  Bit 4: write change is add
  Bit 5: write change is del
  Bit 6: old events had EV_READ
  Bit 7: old events had EV_WRITE
  Bit 8: old events had EV_CLOSED
*/

#define EPOLL_OP_TABLE_INDEX(c) \
	(   (((c)->close_change&(EV_CHANGE_ADD|EV_CHANGE_DEL))) |		\
	    (((c)->read_change&(EV_CHANGE_ADD|EV_CHANGE_DEL)) << 2) |	\
	    (((c)->write_change&(EV_CHANGE_ADD|EV_CHANGE_DEL)) << 4) |	\
	    (((c)->old_events&(EV_READ|EV_WRITE)) << 5) |		\
	    (((c)->old_events&(EV_CLOSED)) << 1)				\
	    )

#if EV_READ != 2 || EV_WRITE != 4 || EV_CLOSED != 0x80 || EV_CHANGE_ADD != 1 || EV_CHANGE_DEL != 2
#error "Libevent's internals changed!  Regenerate the op_table in epolltable-internal.h"
#endif

static const struct operation {
	int events;
	int op;
} epoll_op_table[] = {
	/* old=  0, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old=  0, write:  0, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:  0, read:  0, close:del */
	{ EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  0, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  0, write:  0, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_ADD },
	/* old=  0, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:  0, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_ADD },
	/* old=  0, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old=  0, write:  0, read:del, close:  0 */
	{ EPOLLIN, EPOLL_CTL_DEL },
	/* old=  0, write:  0, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:  0, read:del, close:del */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  0, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old=  0, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  0, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old=  0, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old=  0, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  0, write:add, read:  0, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  0, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old=  0, write:add, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_ADD },
	/* old=  0, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old=  0, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  0, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old=  0, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old=  0, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  0, write:del, read:  0, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_DEL },
	/* old=  0, write:del, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:del, read:  0, close:del */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  0, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  0, write:del, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_ADD },
	/* old=  0, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_ADD },
	/* old=  0, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old=  0, write:del, read:del, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_DEL },
	/* old=  0, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_ADD },
	/* old=  0, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  0, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old=  0, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  0, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old=  0, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old=  0, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  0, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old=  0, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old=  0, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old=  0, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  0, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old=  0, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old=  0, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old=  0, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old=  0, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old=  0, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old=  0, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old=  0, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old=  0, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  0, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old=  0, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old=  0, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  r, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old=  r, write:  0, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  r, write:  0, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old=  r, write:  0, read:del, close:  0 */
	{ EPOLLIN, EPOLL_CTL_DEL },
	/* old=  r, write:  0, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:  0, read:del, close:del */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  r, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old=  r, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  r, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old=  r, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old=  r, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  r, write:add, read:  0, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  r, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old=  r, write:add, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  r, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old=  r, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  r, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old=  r, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old=  r, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  r, write:del, read:  0, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  r, write:del, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old=  r, write:del, read:del, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_DEL },
	/* old=  r, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  r, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  r, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old=  r, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  r, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old=  r, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old=  r, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  r, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old=  r, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old=  r, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old=  r, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  r, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old=  r, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old=  r, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old=  r, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old=  r, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old=  r, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old=  r, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old=  r, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old=  r, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  r, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old=  r, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old=  r, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  w, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old=  w, write:  0, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  w, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old=  w, write:  0, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old=  w, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  w, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old=  w, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old=  w, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  w, write:add, read:  0, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  w, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old=  w, write:add, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  w, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old=  w, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  w, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old=  w, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old=  w, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  w, write:del, read:  0, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_DEL },
	/* old=  w, write:del, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:del, read:  0, close:del */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  w, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  w, write:del, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  w, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  w, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old=  w, write:del, read:del, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_DEL },
	/* old=  w, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  w, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  w, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old=  w, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  w, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old=  w, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old=  w, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  w, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old=  w, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old=  w, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old=  w, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  w, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old=  w, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old=  w, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old=  w, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old=  w, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old=  w, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old=  w, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old=  w, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old=  w, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  w, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old=  w, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old=  w, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= rw, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old= rw, write:  0, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old= rw, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old= rw, write:  0, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old= rw, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= rw, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old= rw, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old= rw, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= rw, write:add, read:  0, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old= rw, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old= rw, write:add, read:del, close:  0 */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= rw, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old= rw, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= rw, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old= rw, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old= rw, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= rw, write:del, read:  0, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old= rw, write:del, read:add, close:  0 */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old= rw, write:del, read:del, close:  0 */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_DEL },
	/* old= rw, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= rw, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old= rw, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old= rw, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= rw, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old= rw, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old= rw, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= rw, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old= rw, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old= rw, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old= rw, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old= rw, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old= rw, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old= rw, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old= rw, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old= rw, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old= rw, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old= rw, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old= rw, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old= rw, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= rw, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old= rw, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old= rw, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  c, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old=  c, write:  0, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:  0, close:del */
	{ EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  c, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  c, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old=  c, write:  0, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:  0, read:del, close:del */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  c, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old=  c, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  c, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old=  c, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old=  c, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  c, write:add, read:  0, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  c, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old=  c, write:add, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=  c, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old=  c, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  c, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old=  c, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old=  c, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  c, write:del, read:  0, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:  0, close:del */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  c, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  c, write:del, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old=  c, write:del, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=  c, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=  c, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old=  c, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  c, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old=  c, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old=  c, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=  c, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old=  c, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old=  c, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old=  c, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old=  c, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old=  c, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old=  c, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old=  c, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old=  c, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old=  c, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old=  c, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old=  c, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old=  c, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=  c, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old=  c, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old=  c, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cr, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old= cr, write:  0, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cr, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old= cr, write:  0, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:  0, read:del, close:del */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old= cr, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old= cr, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cr, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old= cr, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old= cr, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cr, write:add, read:  0, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cr, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old= cr, write:add, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cr, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old= cr, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cr, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old= cr, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old= cr, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cr, write:del, read:  0, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cr, write:del, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old= cr, write:del, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cr, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old= cr, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old= cr, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cr, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old= cr, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old= cr, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cr, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old= cr, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old= cr, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old= cr, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cr, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old= cr, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old= cr, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old= cr, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old= cr, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old= cr, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old= cr, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old= cr, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old= cr, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cr, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old= cr, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old= cr, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cw, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old= cw, write:  0, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cw, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old= cw, write:  0, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old= cw, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cw, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old= cw, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old= cw, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cw, write:add, read:  0, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:  0, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:  0, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cw, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old= cw, write:add, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old= cw, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old= cw, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cw, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old= cw, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old= cw, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cw, write:del, read:  0, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:  0, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:  0, close:del */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old= cw, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cw, write:del, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old= cw, write:del, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old= cw, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old= cw, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old= cw, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cw, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old= cw, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old= cw, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old= cw, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old= cw, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old= cw, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old= cw, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old= cw, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old= cw, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old= cw, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old= cw, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old= cw, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old= cw, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old= cw, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old= cw, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old= cw, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old= cw, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old= cw, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old= cw, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=crw, write:  0, read:  0, close:  0 */
	{ 0, 0 },
	/* old=crw, write:  0, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:  0, close:xxx */
	{ 0, 255 },
	/* old=crw, write:  0, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:add, close:xxx */
	{ 0, 255 },
	/* old=crw, write:  0, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:  0, read:del, close:xxx */
	{ 0, 255 },
	/* old=crw, write:  0, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=crw, write:  0, read:xxx, close:add */
	{ 0, 255 },
	/* old=crw, write:  0, read:xxx, close:del */
	{ 0, 255 },
	/* old=crw, write:  0, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=crw, write:add, read:  0, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:  0, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:  0, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:  0, close:xxx */
	{ 0, 255 },
	/* old=crw, write:add, read:add, close:  0 */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:add, close:add */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:add, close:del */
	{ EPOLLIN|EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:add, close:xxx */
	{ 0, 255 },
	/* old=crw, write:add, read:del, close:  0 */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:del, close:add */
	{ EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:del, close:del */
	{ EPOLLOUT, EPOLL_CTL_MOD },
	/* old=crw, write:add, read:del, close:xxx */
	{ 0, 255 },
	/* old=crw, write:add, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=crw, write:add, read:xxx, close:add */
	{ 0, 255 },
	/* old=crw, write:add, read:xxx, close:del */
	{ 0, 255 },
	/* old=crw, write:add, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=crw, write:del, read:  0, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:  0, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:  0, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:  0, close:xxx */
	{ 0, 255 },
	/* old=crw, write:del, read:add, close:  0 */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:add, close:add */
	{ EPOLLIN|EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:add, close:del */
	{ EPOLLIN, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:add, close:xxx */
	{ 0, 255 },
	/* old=crw, write:del, read:del, close:  0 */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:del, close:add */
	{ EPOLLRDHUP, EPOLL_CTL_MOD },
	/* old=crw, write:del, read:del, close:del */
	{ EPOLLIN|EPOLLOUT|EPOLLRDHUP, EPOLL_CTL_DEL },
	/* old=crw, write:del, read:del, close:xxx */
	{ 0, 255 },
	/* old=crw, write:del, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=crw, write:del, read:xxx, close:add */
	{ 0, 255 },
	/* old=crw, write:del, read:xxx, close:del */
	{ 0, 255 },
	/* old=crw, write:del, read:xxx, close:xxx */
	{ 0, 255 },
	/* old=crw, write:xxx, read:  0, close:  0 */
	{ 0, 255 },
	/* old=crw, write:xxx, read:  0, close:add */
	{ 0, 255 },
	/* old=crw, write:xxx, read:  0, close:del */
	{ 0, 255 },
	/* old=crw, write:xxx, read:  0, close:xxx */
	{ 0, 255 },
	/* old=crw, write:xxx, read:add, close:  0 */
	{ 0, 255 },
	/* old=crw, write:xxx, read:add, close:add */
	{ 0, 255 },
	/* old=crw, write:xxx, read:add, close:del */
	{ 0, 255 },
	/* old=crw, write:xxx, read:add, close:xxx */
	{ 0, 255 },
	/* old=crw, write:xxx, read:del, close:  0 */
	{ 0, 255 },
	/* old=crw, write:xxx, read:del, close:add */
	{ 0, 255 },
	/* old=crw, write:xxx, read:del, close:del */
	{ 0, 255 },
	/* old=crw, write:xxx, read:del, close:xxx */
	{ 0, 255 },
	/* old=crw, write:xxx, read:xxx, close:  0 */
	{ 0, 255 },
	/* old=crw, write:xxx, read:xxx, close:add */
	{ 0, 255 },
	/* old=crw, write:xxx, read:xxx, close:del */
	{ 0, 255 },
	/* old=crw, write:xxx, read:xxx, close:xxx */
	{ 0, 255 },
};

#endif

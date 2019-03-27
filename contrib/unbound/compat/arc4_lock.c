/* arc4_lock.c - global lock for arc4random
*
 * Copyright (c) 2014, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "config.h"
#define LOCKRET(func) func
#ifdef ENABLE_LOCK_CHECKS
#undef ENABLE_LOCK_CHECKS
#endif
#include "util/locks.h"

void _ARC4_LOCK(void);
void _ARC4_UNLOCK(void);

#ifdef THREADS_DISABLED
void _ARC4_LOCK(void)
{
}

void _ARC4_UNLOCK(void)
{
}

void _ARC4_LOCK_DESTROY(void)
{
}
#else /* !THREADS_DISABLED */

static lock_quick_type arc4lock;
static int arc4lockinit = 0;

void _ARC4_LOCK(void)
{
	if(!arc4lockinit) {
		arc4lockinit = 1;
		lock_quick_init(&arc4lock);
	}
	lock_quick_lock(&arc4lock);
}

void _ARC4_UNLOCK(void)
{
	lock_quick_unlock(&arc4lock);
}

void _ARC4_LOCK_DESTROY(void)
{
	if(arc4lockinit) {
		arc4lockinit = 0;
		lock_quick_destroy(&arc4lock);
	}
}
#endif /* THREADS_DISABLED */

/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
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
#ifndef EVENT2_VISIBILITY_H_INCLUDED_
#define EVENT2_VISIBILITY_H_INCLUDED_

#include <event2/event-config.h>

#if defined(event_EXPORTS) || defined(event_extra_EXPORTS) || defined(event_core_EXPORTS)
# if defined (__SUNPRO_C) && (__SUNPRO_C >= 0x550)
#  define EVENT2_EXPORT_SYMBOL __global
# elif defined __GNUC__
#  define EVENT2_EXPORT_SYMBOL __attribute__ ((visibility("default")))
# elif defined(_MSC_VER)
#  define EVENT2_EXPORT_SYMBOL extern __declspec(dllexport)
# else
#  define EVENT2_EXPORT_SYMBOL /* unknown compiler */
# endif
#else
# if defined(EVENT__NEED_DLLIMPORT) && defined(_MSC_VER) && !defined(EVENT_BUILDING_REGRESS_TEST)
#  define EVENT2_EXPORT_SYMBOL extern __declspec(dllimport)
# else
#  define EVENT2_EXPORT_SYMBOL
# endif
#endif

#endif /* EVENT2_VISIBILITY_H_INCLUDED_ */

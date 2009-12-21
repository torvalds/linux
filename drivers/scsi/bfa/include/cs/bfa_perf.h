/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __BFAD_PERF_H__
#define __BFAD_PERF_H__

#ifdef BFAD_PERF_BUILD

#undef bfa_trc
#undef bfa_trc32
#undef bfa_assert
#undef BFA_TRC_FILE

#define bfa_trc(_trcp, _data)
#define bfa_trc32(_trcp, _data)
#define bfa_assert(__cond)
#define BFA_TRC_FILE(__mod, __submod)

#endif

#endif /* __BFAD_PERF_H__ */

/*
 * drivers/net/ethernet/mellanox/mlxsw/txheader.h
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_TXHEADER_H
#define _MLXSW_TXHEADER_H

#define MLXSW_TXHDR_LEN 0x10
#define MLXSW_TXHDR_VERSION_0 0
#define MLXSW_TXHDR_VERSION_1 1

enum {
	MLXSW_TXHDR_ETH_CTL,
	MLXSW_TXHDR_ETH_DATA,
};

#define MLXSW_TXHDR_PROTO_ETH 1

enum {
	MLXSW_TXHDR_ETCLASS_0,
	MLXSW_TXHDR_ETCLASS_1,
	MLXSW_TXHDR_ETCLASS_2,
	MLXSW_TXHDR_ETCLASS_3,
	MLXSW_TXHDR_ETCLASS_4,
	MLXSW_TXHDR_ETCLASS_5,
	MLXSW_TXHDR_ETCLASS_6,
	MLXSW_TXHDR_ETCLASS_7,
};

enum {
	MLXSW_TXHDR_RDQ_OTHER,
	MLXSW_TXHDR_RDQ_EMAD = 0x1f,
};

#define MLXSW_TXHDR_CTCLASS3 0
#define MLXSW_TXHDR_CPU_SIG 0
#define MLXSW_TXHDR_SIG 0xE0E0
#define MLXSW_TXHDR_STCLASS_NONE 0

enum {
	MLXSW_TXHDR_NOT_EMAD,
	MLXSW_TXHDR_EMAD,
};

enum {
	MLXSW_TXHDR_TYPE_DATA,
	MLXSW_TXHDR_TYPE_CONTROL = 6,
};

#endif

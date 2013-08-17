/*
 * <-- Copyright Giesecke & Devrient GmbH 2011-2012 -->
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *	products derived from this software without specific prior
 *	written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _MCUUID_H_
#define _MCUUID_H_

#define UUID_TYPE

/* Universally Unique Identifier (UUID) according to ISO/IEC 11578. */
struct mc_uuid_t {
	uint8_t		value[16];	/* Value of the UUID. */
};

/* UUID value used as free marker in service provider containers. */
#define MC_UUID_FREE_DEFINE \
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, \
	  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

static const struct mc_uuid_t MC_UUID_FREE = {
	MC_UUID_FREE_DEFINE
};

/* Reserved UUID. */
#define MC_UUID_RESERVED_DEFINE \
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

static const struct mc_uuid_t MC_UUID_RESERVED = {
	MC_UUID_RESERVED_DEFINE
};

/* UUID for system applications. */
#define MC_UUID_SYSTEM_DEFINE \
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, \
	  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE }

static const struct mc_uuid_t MC_UUID_SYSTEM = {
	MC_UUID_SYSTEM_DEFINE
};

#endif /* _MCUUID_H_ */

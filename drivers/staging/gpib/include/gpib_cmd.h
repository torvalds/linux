/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _GPIB_CMD_H
#define _GPIB_CMD_H

#include <linux/types.h>

/* Command byte definitions tests and functions */

/* mask of bits that actually matter in a command byte */
enum {
	gpib_command_mask = 0x7f,
};

/* Possible GPIB command messages */

enum cmd_byte {
	GTL = 0x1,	/* go to local			*/
	SDC = 0x4,	/* selected device clear	*/
	PP_CONFIG = 0x5,
	GET = 0x8,	/* group execute trigger	*/
	TCT = 0x9,	/* take control			*/
	LLO = 0x11,	/* local lockout		*/
	DCL = 0x14,	/* device clear			*/
	PPU = 0x15,	/* parallel poll unconfigure	*/
	SPE = 0x18,	/* serial poll enable		*/
	SPD = 0x19,	/* serial poll disable		*/
	CFE = 0x1f,     /* configure enable */
	LAD = 0x20,	/* value to be 'ored' in to obtain listen address */
	UNL = 0x3F,	/* unlisten			*/
	TAD = 0x40,	/* value to be 'ored' in to obtain talk address	  */
	UNT = 0x5F,	/* untalk			*/
	SAD = 0x60,	/* my secondary address (base) */
	PPE = 0x60,	/* parallel poll enable (base)	*/
	PPD = 0x70	/* parallel poll disable	*/
};

/* confine address to range 0 to 30. */
static inline unsigned int gpib_address_restrict(u32 addr)
{
	addr &= 0x1f;
	if (addr == 0x1f)
		addr = 0;
	return addr;
}

static inline u8 MLA(u32 addr)
{
	return gpib_address_restrict(addr) | LAD;
}

static inline u8 MTA(u32 addr)
{
	return gpib_address_restrict(addr) | TAD;
}

static inline u8 MSA(u32 addr)
{
	return (addr & 0x1f) | SAD;
}

static inline s32 gpib_address_equal(u32 pad1, s32 sad1, u32 pad2, s32 sad2)
{
	if (pad1 == pad2) {
		if (sad1 == sad2)
			return 1;
		if (sad1 < 0 && sad2 < 0)
			return 1;
	}

	return 0;
}

static inline s32 is_PPE(u8 command)
{
	return (command & 0x70) == 0x60;
}

static inline s32 is_PPD(u8 command)
{
	return (command & 0x70) == 0x70;
}

static inline s32 in_addressed_command_group(u8 command)
{
	return (command & 0x70) == 0x0;
}

static inline s32 in_universal_command_group(u8 command)
{
	return (command & 0x70) == 0x10;
}

static inline s32 in_listen_address_group(u8 command)
{
	return (command & 0x60) == 0x20;
}

static inline s32 in_talk_address_group(u8 command)
{
	return (command & 0x60) == 0x40;
}

static inline s32 in_primary_command_group(u8 command)
{
	return in_addressed_command_group(command) ||
		in_universal_command_group(command) ||
		in_listen_address_group(command) ||
		in_talk_address_group(command);
}

#endif /* _GPIB_CMD_H */

/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_COMMANDS_H_
#define _XE_GSC_COMMANDS_H_

#include "instructions/xe_instr_defs.h"

/*
 * All GSCCS-specific commands have fixed length, so we can include it in the
 * defines. Note that the generic GSC command header structure includes an
 * optional data field in bits 9-21, but there are no commands that actually use
 * it; some of the commands are instead defined as having an extended length
 * field spanning bits 0-15, even if the extra bits are not required because the
 * longest GSCCS command is only 8 dwords. To handle this, the defines below use
 * a single field for both data and len. If we ever get a commands that does
 * actually have data and this approach doesn't work for it we can re-work it
 * at that point.
 */

#define GSC_OPCODE		REG_GENMASK(28, 22)
#define GSC_CMD_DATA_AND_LEN	REG_GENMASK(21, 0)

#define __GSC_INSTR(op, dl) \
	(XE_INSTR_GSC | \
	REG_FIELD_PREP(GSC_OPCODE, op) | \
	REG_FIELD_PREP(GSC_CMD_DATA_AND_LEN, dl))

#define GSC_HECI_CMD_PKT __GSC_INSTR(0, 6)

#define GSC_FW_LOAD __GSC_INSTR(1, 2)
#define   GSC_FW_LOAD_LIMIT_VALID REG_BIT(31)

#endif

/*
 * Copyright (C) 2007, David Kilroy
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */
#ifndef _HERMES_DLD_H
#define _HERMES_DLD_H

#include "hermes.h"

/* Position of PDA in the adapter memory */
#define EEPROM_ADDR	0x3000
#define EEPROM_LEN	0x200
#define PDA_OFFSET	0x100

#define PDA_ADDR	(EEPROM_ADDR + PDA_OFFSET)
#define PDA_WORDS	((EEPROM_LEN - PDA_OFFSET) / 2)

struct dblock;

int spectrum_read_pda(hermes_t *hw, __le16 *pda, int pda_len);
int spectrum_apply_pda(hermes_t *hw, const struct dblock *first_block,
		       __le16 *pda);
int spectrum_load_blocks(hermes_t *hw, const struct dblock *first_block);

#endif /* _HERMES_DLD_H */

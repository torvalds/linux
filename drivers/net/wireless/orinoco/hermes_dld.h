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

int hermesi_program_init(hermes_t *hw, u32 offset);
int hermesi_program_end(hermes_t *hw);
int hermes_program(hermes_t *hw, const char *first_block, const char *end);

int hermes_read_pda(hermes_t *hw,
		    __le16 *pda,
		    u32 pda_addr,
		    u16 pda_len,
		    int use_eeprom);
int hermes_apply_pda(hermes_t *hw,
		     const char *first_pdr,
		     const __le16 *pda);
int hermes_apply_pda_with_defaults(hermes_t *hw,
				   const char *first_pdr,
				   const __le16 *pda);

size_t hermes_blocks_length(const char *first_block);

#endif /* _HERMES_DLD_H */

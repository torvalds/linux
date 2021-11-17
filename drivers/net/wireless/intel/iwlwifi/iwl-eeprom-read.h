/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014 Intel Corporation
 */
#ifndef __iwl_eeprom_h__
#define __iwl_eeprom_h__

#include "iwl-trans.h"

int iwl_read_eeprom(struct iwl_trans *trans, u8 **eeprom, size_t *eeprom_size);

#endif  /* __iwl_eeprom_h__ */

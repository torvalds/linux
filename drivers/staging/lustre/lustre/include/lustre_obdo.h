/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2014, Intel Corporation.
 *
 * Copyright 2015 Cray Inc, all rights reserved.
 * Author: Ben Evans.
 *
 * Define obdo associated functions
 *   obdo:  OBject Device o...
 */

#ifndef _LUSTRE_OBDO_H_
#define _LUSTRE_OBDO_H_

#include "lustre/lustre_idl.h"

/**
 * Create an obdo to send over the wire
 */
void lustre_set_wire_obdo(const struct obd_connect_data *ocd,
			  struct obdo *wobdo,
			  const struct obdo *lobdo);

/**
 * Create a local obdo from a wire based odbo
 */
void lustre_get_wire_obdo(const struct obd_connect_data *ocd,
			  struct obdo *lobdo,
			  const struct obdo *wobdo);

#endif

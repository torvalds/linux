/*
 * services.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Provide loading and unloading of SERVICES modules.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef SERVICES_
#define SERVICES_

#include <dspbridge/host_os.h>
/*
 *  ======== services_exit ========
 *  Purpose:
 *      Discontinue usage of module; free resources when reference count
 *      reaches 0.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      SERVICES initialized.
 *  Ensures:
 *      Resources used by module are freed when cRef reaches zero.
 */
extern void services_exit(void);

/*
 *  ======== services_init ========
 *  Purpose:
 *      Initializes SERVICES modules.
 *  Parameters:
 *  Returns:
 *      TRUE if all modules initialized; otherwise FALSE.
 *  Requires:
 *  Ensures:
 *      SERVICES modules initialized.
 */
extern bool services_init(void);

#endif /* SERVICES_ */

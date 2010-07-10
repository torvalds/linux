/*
 * cfg.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * PM Configuration module.
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

#ifndef CFG_
#define CFG_
#include <dspbridge/host_os.h>
#include <dspbridge/cfgdefs.h>

/*
 *  ======== cfg_exit ========
 *  Purpose:
 *      Discontinue usage of the CFG module.
 *  Parameters:
 *  Returns:
 *  Requires:
 *      cfg_init(void) was previously called.
 *  Ensures:
 *      Resources acquired in cfg_init(void) are freed.
 */
extern void cfg_exit(void);

/*
 *  ======== cfg_get_auto_start ========
 *  Purpose:
 *      Retreive the autostart mask, if any, for this board.
 *  Parameters:
 *      dev_node_obj:  Handle to the dev_node who's driver we are querying.
 *      auto_start:   Ptr to location for 32 bit autostart mask.
 *  Returns:
 *      0:                Success.
 *      -EFAULT:  dev_node_obj is invalid.
 *      -ENODATA: Unable to retreive resource.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:        *auto_start contains autostart mask for this devnode.
 */
extern int cfg_get_auto_start(IN struct cfg_devnode *dev_node_obj,
				     OUT u32 *auto_start);

/*
 *  ======== cfg_get_cd_version ========
 *  Purpose:
 *      Retrieves the version of the PM Class Driver.
 *  Parameters:
 *      pdwVersion: Ptr to u32 to contain version number upon return.
 *  Returns:
 *      0:    Success.  pdwVersion contains Class Driver version in
 *                  the form: 0xAABBCCDD where AABB is Major version and
 *                  CCDD is Minor.
 *      -EPERM:  Failure.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    Success.
 *      else:       *pdwVersion is NULL.
 */
extern int cfg_get_cd_version(OUT u32 *pdwVersion);

/*
 *  ======== cfg_get_dev_object ========
 *  Purpose:
 *      Retrieve the Device Object handle for a given devnode.
 *  Parameters:
 *      dev_node_obj:	Platform's dev_node handle from which to retrieve
 *      		value.
 *      pdwValue:       Ptr to location to store the value.
 *  Returns:
 *      0:                Success.
 *      -EFAULT: dev_node_obj is invalid or phDevObject is invalid.
 *      -ENODATA: The resource is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    *pdwValue is set to the retrieved u32.
 *      else:       *pdwValue is set to 0L.
 */
extern int cfg_get_dev_object(IN struct cfg_devnode *dev_node_obj,
				     OUT u32 *pdwValue);

/*
 *  ======== cfg_get_exec_file ========
 *  Purpose:
 *      Retreive the default executable, if any, for this board.
 *  Parameters:
 *      dev_node_obj: Handle to the dev_node who's driver we are querying.
 *      buf_size:       Size of buffer.
 *      pstrExecFile:   Ptr to character buf to hold ExecFile.
 *  Returns:
 *      0:                Success.
 *      -EFAULT:  dev_node_obj is invalid or pstrExecFile is invalid.
 *      -ENODATA: The resource is not available.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    Not more than buf_size bytes were copied into pstrExecFile,
 *                  and *pstrExecFile contains default executable for this
 *                  devnode.
 */
extern int cfg_get_exec_file(IN struct cfg_devnode *dev_node_obj,
				    IN u32 buf_size, OUT char *pstrExecFile);

/*
 *  ======== cfg_get_object ========
 *  Purpose:
 *      Retrieve the Driver Object handle From the Registry
 *  Parameters:
 *      pdwValue:   Ptr to location to store the value.
 *      dw_type      Type of Object to Get
 *  Returns:
 *      0:    Success.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    *pdwValue is set to the retrieved u32(non-Zero).
 *      else:       *pdwValue is set to 0L.
 */
extern int cfg_get_object(OUT u32 *pdwValue, u8 dw_type);

/*
 *  ======== cfg_get_perf_value ========
 *  Purpose:
 *      Retrieve a flag indicating whether PERF should log statistics for the
 *      PM class driver.
 *  Parameters:
 *      pfEnablePerf:   Location to store flag.  0 indicates the key was
 *                      not found, or had a zero value.  A nonzero value
 *                      means the key was found and had a nonzero value.
 *  Returns:
 *  Requires:
 *      pfEnablePerf != NULL;
 *  Ensures:
 */
extern void cfg_get_perf_value(OUT bool *pfEnablePerf);

/*
 *  ======== cfg_get_zl_file ========
 *  Purpose:
 *      Retreive the ZLFile, if any, for this board.
 *  Parameters:
 *      dev_node_obj: Handle to the dev_node who's driver we are querying.
 *      buf_size:       Size of buffer.
 *      pstrZLFileName: Ptr to character buf to hold ZLFileName.
 *  Returns:
 *      0:                Success.
 *      -EFAULT: pstrZLFileName is invalid or dev_node_obj is invalid.
 *      -ENODATA: couldn't find the ZLFileName.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    Not more than buf_size bytes were copied into
 *                  pstrZLFileName, and *pstrZLFileName contains ZLFileName
 *                  for this devnode.
 */
extern int cfg_get_zl_file(IN struct cfg_devnode *dev_node_obj,
				  IN u32 buf_size, OUT char *pstrZLFileName);

/*
 *  ======== cfg_init ========
 *  Purpose:
 *      Initialize the CFG module's private state.
 *  Parameters:
 *  Returns:
 *      TRUE if initialized; FALSE if error occured.
 *  Requires:
 *  Ensures:
 *      A requirement for each of the other public CFG functions.
 */
extern bool cfg_init(void);

/*
 *  ======== cfg_set_dev_object ========
 *  Purpose:
 *      Store the Device Object handle for a given devnode.
 *  Parameters:
 *      dev_node_obj:   Platform's dev_node handle we are storing value with.
 *      value:    Arbitrary value to store.
 *  Returns:
 *      0:                Success.
 *      -EFAULT:  dev_node_obj is invalid.
 *      -EPERM:              Internal Error.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:    The Private u32 was successfully set.
 */
extern int cfg_set_dev_object(IN struct cfg_devnode *dev_node_obj,
				     IN u32 value);

/*
 *  ======== CFG_SetDrvObject ========
 *  Purpose:
 *      Store the Driver Object handle.
 *  Parameters:
 *      value:          Arbitrary value to store.
 *      dw_type          Type of Object to Store
 *  Returns:
 *      0:        Success.
 *      -EPERM:      Internal Error.
 *  Requires:
 *      CFG initialized.
 *  Ensures:
 *      0:        The Private u32 was successfully set.
 */
extern int cfg_set_object(IN u32 value, u8 dw_type);

#endif /* CFG_ */

/**
 ******************************************************************************
 *
 * @file sdio_host_interface.h
 *
 * @brief sdio host interface definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */
 
#ifndef _SDIO_HOST_INTERFACE_H
#define _SDIO_HOST_INTERFACE_H
/*******************************************************************************
 * Function: sdio_xmit
 * Description:send buff from host to slave
 * Parameters: 
 *   Input: void *buff, int len, int flag
 *
 *   Output:
 *
 * Returns: 0
 *
 *
 * Others: 
 ********************************************************************************/
int sdio_host_send(void *buff, int len, int flag);
#endif
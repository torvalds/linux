/**
 ******************************************************************************
 *
 * @file usb_host_interface.h
 *
 * @brief usb host interface definitions
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _USB_HOST_INTERFACE_H
#define _USB_HOST_INTERFACE_H
/*******************************************************************************
 * Function: usb_host_send
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
int usb_host_send(void *buff, int len, int flag);
#endif

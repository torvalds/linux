/**
******************************************************************************
*
* @file fw.h
*
* @brief ecrnx usb firmware download definitions
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#ifndef _FW_H_
#define _FW_H_

char eswin_fw_file_download(struct eswin *tr);
bool eswin_fw_file_chech(struct eswin *tr);

#endif

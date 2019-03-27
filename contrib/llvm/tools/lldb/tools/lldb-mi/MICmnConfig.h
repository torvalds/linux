//===-- MICmnConfig.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//--
#pragma once

// 1 = Show debug process attach modal dialog, 0 = do not show
// For windows only ATM, other OS's code is an infinite loop which a debugger
// must change a value to continue
#define MICONFIG_DEBUG_SHOW_ATTACH_DBG_DLG 0

// 1 = Write to MI's Log file warnings about commands that did not handle
// arguments or
// options present to them by the driver's client, 0 = no warnings given
#define MICONFIG_GIVE_WARNING_CMD_ARGS_NOT_HANDLED 1

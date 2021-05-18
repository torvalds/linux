// SPDX-License-Identifier: GPL-2.0+
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */
/*
 *  comedi/drivers/ni_routing/ni_device_routes/pci-6723.c
 *  List of valid routes for specific NI boards.
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2016 Spencer E. Olson <olsonse@umich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * The contents of this file are generated using the tools in
 * comedi/drivers/ni_routing/tools
 *
 * Please use those tools to help maintain the contents of this file.
 */

#include "../ni_device_routes.h"
#include "all.h"

struct ni_device_routes ni_pci_6723_device_routes = {
	.device = "pci-6723",
	.routes = (struct ni_route_set[]){
		{
			.dest = NI_PFI(3),
			.src = (int[]){
				NI_CtrSource(1),
				0, /* Termination */
			}
		},
		{
			.dest = NI_PFI(4),
			.src = (int[]){
				NI_CtrGate(1),
				0, /* Termination */
			}
		},
		{
			.dest = NI_PFI(5),
			.src = (int[]){
				NI_AO_SampleClock,
				0, /* Termination */
			}
		},
		{
			.dest = NI_PFI(6),
			.src = (int[]){
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = NI_PFI(8),
			.src = (int[]){
				NI_CtrSource(0),
				0, /* Termination */
			}
		},
		{
			.dest = NI_PFI(9),
			.src = (int[]){
				NI_CtrGate(0),
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(0),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(1),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(2),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(3),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(4),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(5),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(6),
			.src = (int[]){
				NI_CtrSource(0),
				NI_CtrGate(0),
				NI_CtrInternalOutput(0),
				NI_CtrOut(0),
				NI_AO_SampleClock,
				NI_AO_StartTrigger,
				0, /* Termination */
			}
		},
		{
			.dest = TRIGGER_LINE(7),
			.src = (int[]){
				NI_20MHzTimebase,
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrSource(0),
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				TRIGGER_LINE(7),
				NI_MasterTimebase,
				NI_20MHzTimebase,
				NI_100kHzTimebase,
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrSource(1),
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				TRIGGER_LINE(7),
				NI_MasterTimebase,
				NI_20MHzTimebase,
				NI_100kHzTimebase,
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrGate(0),
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				NI_CtrInternalOutput(1),
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrGate(1),
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				NI_CtrInternalOutput(0),
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrOut(0),
			.src = (int[]){
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				NI_CtrInternalOutput(0),
				0, /* Termination */
			}
		},
		{
			.dest = NI_CtrOut(1),
			.src = (int[]){
				NI_CtrInternalOutput(1),
				0, /* Termination */
			}
		},
		{
			.dest = NI_AO_SampleClock,
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				NI_CtrInternalOutput(1),
				NI_AO_SampleClockTimebase,
				0, /* Termination */
			}
		},
		{
			.dest = NI_AO_SampleClockTimebase,
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				TRIGGER_LINE(7),
				NI_MasterTimebase,
				NI_20MHzTimebase,
				NI_100kHzTimebase,
				0, /* Termination */
			}
		},
		{
			.dest = NI_AO_StartTrigger,
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				0, /* Termination */
			}
		},
		{
			.dest = NI_AO_PauseTrigger,
			.src = (int[]){
				NI_PFI(0),
				NI_PFI(1),
				NI_PFI(2),
				NI_PFI(3),
				NI_PFI(4),
				NI_PFI(5),
				NI_PFI(6),
				NI_PFI(7),
				NI_PFI(8),
				NI_PFI(9),
				TRIGGER_LINE(0),
				TRIGGER_LINE(1),
				TRIGGER_LINE(2),
				TRIGGER_LINE(3),
				TRIGGER_LINE(4),
				TRIGGER_LINE(5),
				TRIGGER_LINE(6),
				0, /* Termination */
			}
		},
		{
			.dest = NI_MasterTimebase,
			.src = (int[]){
				TRIGGER_LINE(7),
				NI_20MHzTimebase,
				0, /* Termination */
			}
		},
		{ /* Termination of list */
			.dest = 0,
		},
	},
};

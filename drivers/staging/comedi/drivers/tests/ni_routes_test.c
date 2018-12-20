// SPDX-License-Identifier: GPL-2.0+
/* vim: set ts=8 sw=8 noet tw=80 nowrap: */
/*
 *  comedi/drivers/tests/ni_routes_test.c
 *  Unit tests for NI routes (ni_routes.c module).
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

#include <linux/module.h>

#include "../ni_stc.h"
#include "../ni_routes.h"
#include "unittest.h"

#define RVi(table, src, dest)	((table)[(dest) * NI_NUM_NAMES + (src)])
#define O(x)	((x) + NI_NAMES_BASE)
#define B(x)	((x) - NI_NAMES_BASE)
#define V(x)	((x) | 0x80)

/* *** BEGIN fake board data *** */
static const char *pci_6070e = "pci-6070e";
static const char *pci_6220 = "pci-6220";
static const char *pci_fake = "pci-fake";

static const char *ni_eseries = "ni_eseries";
static const char *ni_mseries = "ni_mseries";

static struct ni_board_struct board = {
	.name = NULL,
};

static struct ni_private private = {
	.is_m_series = 0,
};

static const int bad_dest = O(8), dest0 = O(0), desti = O(5);
static const int ith_dest_index = 2;
static const int no_val_dest = O(7), no_val_index = 4;

/* These have to be defs to be used in init code below */
#define rgout0_src0	(O(100))
#define rgout0_src1	(O(101))
#define brd0_src0	(O(110))
#define brd0_src1	(O(111))
#define brd1_src0	(O(120))
#define brd1_src1	(O(121))
#define brd2_src0	(O(130))
#define brd2_src1	(O(131))
#define brd3_src0	(O(140))
#define brd3_src1	(O(141))

/* I1 and I2 should not call O(...).  Mostly here to shut checkpatch.pl up */
#define I1(x1)	\
	(int[]){ \
		x1, 0 \
	}
#define I2(x1, x2)	\
	(int[]){ \
		(x1), (x2), 0 \
	}
#define I3(x1, x2, x3)	\
	(int[]){ \
		(x1), (x2), (x3), 0 \
	}

/* O9 is build to call O(...) for each arg */
#define O9(x1, x2, x3, x4, x5, x6, x7, x8, x9)	\
	(int[]){ \
		O(x1), O(x2), O(x3), O(x4), O(x5), O(x6), O(x7), O(x8), O(x9), \
		0 \
	}

static struct ni_device_routes DR = {
	.device = "testdev",
	.routes = (struct ni_route_set[]){
		{.dest = O(0), .src = O9(/**/1, 2, 3, 4, 5, 6, 7, 8, 9)},
		{.dest = O(1), .src = O9(0, /**/2, 3, 4, 5, 6, 7, 8, 9)},
		/* ith route_set */
		{.dest = O(5), .src = O9(0, 1, 2, 3, 4,/**/ 6, 7, 8, 9)},
		{.dest = O(6), .src = O9(0, 1, 2, 3, 4, 5,/**/ 7, 8, 9)},
		/* next one will not have valid reg values */
		{.dest = O(7), .src = O9(0, 1, 2, 3, 4, 5, 6,/**/ 8, 9)},
		{.dest = O(9), .src = O9(0, 1, 2, 3, 4, 5, 6, 7, 8/**/)},

		/* indirect routes done through muxes */
		{.dest = TRIGGER_LINE(0), .src = I1(rgout0_src0)},
		{.dest = TRIGGER_LINE(1), .src = I3(rgout0_src0,
						    brd3_src0,
						    brd3_src1)},
		{.dest = TRIGGER_LINE(2), .src = I3(rgout0_src1,
						    brd2_src0,
						    brd2_src1)},
		{.dest = TRIGGER_LINE(3), .src = I3(rgout0_src1,
						    brd1_src0,
						    brd1_src1)},
		{.dest = TRIGGER_LINE(4), .src = I2(brd0_src0,
						    brd0_src1)},
		{.dest = 0},
	},
};

#undef I1
#undef I2
#undef O9

#define RV9(x1, x2, x3, x4, x5, x6, x7, x8, x9) \
	[x1] = V(x1), [x2] = V(x2), [x3] = V(x3), [x4] = V(x4), \
	[x5] = V(x5), [x6] = V(x6), [x7] = V(x7), [x8] = V(x8), \
	[x9] = V(x9),

/* This table is indexed as RV[destination][source] */
static const u8 RV[NI_NUM_NAMES][NI_NUM_NAMES] = {
	[0] = {RV9(/**/1, 2, 3, 4, 5, 6, 7, 8, 9)},
	[1] = {RV9(0,/**/ 2, 3, 4, 5, 6, 7, 8, 9)},
	[2] = {RV9(0,  1,/**/3, 4, 5, 6, 7, 8, 9)},
	[3] = {RV9(0,  1, 2,/**/4, 5, 6, 7, 8, 9)},
	[4] = {RV9(0,  1, 2, 3,/**/5, 6, 7, 8, 9)},
	[5] = {RV9(0,  1, 2, 3, 4,/**/6, 7, 8, 9)},
	[6] = {RV9(0,  1, 2, 3, 4, 5,/**/7, 8, 9)},
	/* [7] is intentionaly left absent to test invalid routes */
	[8] = {RV9(0,  1, 2, 3, 4, 5, 6, 7,/**/9)},
	[9] = {RV9(0,  1, 2, 3, 4, 5, 6, 7, 8/**/)},
	/* some tests for needing extra muxes */
	[B(NI_RGOUT0)]	= {[B(rgout0_src0)]   = V(0),
			   [B(rgout0_src1)]   = V(1)},
	[B(NI_RTSI_BRD(0))] = {[B(brd0_src0)] = V(0),
			       [B(brd0_src1)] = V(1)},
	[B(NI_RTSI_BRD(1))] = {[B(brd1_src0)] = V(0),
			       [B(brd1_src1)] = V(1)},
	[B(NI_RTSI_BRD(2))] = {[B(brd2_src0)] = V(0),
			       [B(brd2_src1)] = V(1)},
	[B(NI_RTSI_BRD(3))] = {[B(brd3_src0)] = V(0),
			       [B(brd3_src1)] = V(1)},
};

#undef RV9

/* *** END fake board data *** */

/* *** BEGIN board data initializers *** */
static void init_private(void)
{
	memset(&private, 0, sizeof(struct ni_private));
}

static void init_pci_6070e(void)
{
	board.name = pci_6070e;
	init_private();
	private.is_m_series = 0;
}

static void init_pci_6220(void)
{
	board.name = pci_6220;
	init_private();
	private.is_m_series = 1;
}

static void init_pci_fake(void)
{
	board.name = pci_fake;
	init_private();
	private.routing_tables.route_values = &RV[0][0];
	private.routing_tables.valid_routes = &DR;
}

/* *** END board data initializers *** */

/* Tests that route_sets are in order of the signal destination. */
static bool route_set_dests_in_order(const struct ni_device_routes *devroutes)
{
	int i;
	int last = NI_NAMES_BASE - 1;

	for (i = 0; i < devroutes->n_route_sets; ++i) {
		if (last >= devroutes->routes[i].dest)
			return false;
		last = devroutes->routes[i].dest;
	}
	return true;
}

/* Tests that all route_set->src are in order of the signal source. */
bool route_set_sources_in_order(const struct ni_device_routes *devroutes)
{
	int i;

	for (i = 0; i < devroutes->n_route_sets; ++i) {
		int j;
		int last = NI_NAMES_BASE - 1;

		for (j = 0; j < devroutes->routes[i].n_src; ++j) {
			if (last >= devroutes->routes[i].src[j])
				return false;
			last = devroutes->routes[i].src[j];
		}
	}
	return true;
}

void test_ni_assign_device_routes(void)
{
	const struct ni_device_routes *devroutes, *olddevroutes;
	const u8 *table, *oldtable;

	init_pci_6070e();
	ni_assign_device_routes(ni_eseries, pci_6070e, &private.routing_tables);
	devroutes = private.routing_tables.valid_routes;
	table = private.routing_tables.route_values;

	unittest(strncmp(devroutes->device, pci_6070e, 10) == 0,
		 "find device pci-6070e\n");
	unittest(devroutes->n_route_sets == 37,
		 "number of pci-6070e route_sets == 37\n");
	unittest(devroutes->routes->dest == NI_PFI(0),
		 "first pci-6070e route_set is for NI_PFI(0)\n");
	unittest(devroutes->routes->n_src == 1,
		 "first pci-6070e route_set length == 1\n");
	unittest(devroutes->routes->src[0] == NI_AI_StartTrigger,
		 "first pci-6070e route_set src. == NI_AI_StartTrigger\n");
	unittest(devroutes->routes[10].dest == TRIGGER_LINE(0),
		 "10th pci-6070e route_set is for TRIGGER_LINE(0)\n");
	unittest(devroutes->routes[10].n_src == 10,
		 "10th pci-6070e route_set length == 10\n");
	unittest(devroutes->routes[10].src[0] == NI_CtrSource(0),
		 "10th pci-6070e route_set src. == NI_CtrSource(0)\n");
	unittest(route_set_dests_in_order(devroutes),
		 "all pci-6070e route_sets in order of signal destination\n");
	unittest(route_set_sources_in_order(devroutes),
		 "all pci-6070e route_set->src's in order of signal source\n");

	unittest(
	  RVi(table, B(PXI_Star), B(NI_AI_SampleClock)) == V(17) &&
	  RVi(table, B(NI_10MHzRefClock), B(TRIGGER_LINE(0))) == 0 &&
	  RVi(table, B(NI_AI_ConvertClock), B(NI_PFI(0))) == 0 &&
	  RVi(table, B(NI_AI_ConvertClock), B(NI_PFI(2))) ==
		V(NI_PFI_OUTPUT_AI_CONVERT),
	  "pci-6070e finds e-series route_values table\n");

	olddevroutes = devroutes;
	oldtable = table;
	init_pci_6220();
	ni_assign_device_routes(ni_mseries, pci_6220, &private.routing_tables);
	devroutes = private.routing_tables.valid_routes;
	table = private.routing_tables.route_values;

	unittest(strncmp(devroutes->device, pci_6220, 10) == 0,
		 "find device pci-6220\n");
	unittest(oldtable != table, "pci-6220 find other route_values table\n");

	unittest(
	  RVi(table, B(PXI_Star), B(NI_AI_SampleClock)) == V(20) &&
	  RVi(table, B(NI_10MHzRefClock), B(TRIGGER_LINE(0))) == V(12) &&
	  RVi(table, B(NI_AI_ConvertClock), B(NI_PFI(0))) == V(3) &&
	  RVi(table, B(NI_AI_ConvertClock), B(NI_PFI(2))) == V(3),
	  "pci-6220 finds m-series route_values table\n");
}

void test_ni_sort_device_routes(void)
{
	/* We begin by sorting the device routes for use in later tests */
	ni_sort_device_routes(&DR);
	/* now we test that sorting. */
	unittest(route_set_dests_in_order(&DR),
		 "all route_sets of fake data in order of sig. destination\n");
	unittest(route_set_sources_in_order(&DR),
		 "all route_set->src's of fake data in order of sig. source\n");
}

void test_ni_find_route_set(void)
{
	unittest(ni_find_route_set(bad_dest, &DR) == NULL,
		 "check for nonexistent route_set\n");
	unittest(ni_find_route_set(dest0, &DR) == &DR.routes[0],
		 "find first route_set\n");
	unittest(ni_find_route_set(desti, &DR) == &DR.routes[ith_dest_index],
		 "find ith route_set\n");
	unittest(ni_find_route_set(no_val_dest, &DR) ==
		 &DR.routes[no_val_index],
		 "find no_val route_set in spite of missing values\n");
	unittest(ni_find_route_set(DR.routes[DR.n_route_sets - 1].dest, &DR) ==
		 &DR.routes[DR.n_route_sets - 1],
		 "find last route_set\n");
}

void test_ni_route_set_has_source(void)
{
	unittest(!ni_route_set_has_source(&DR.routes[0], O(0)),
		 "check for bad source\n");
	unittest(ni_route_set_has_source(&DR.routes[0], O(1)),
		 "find first source\n");
	unittest(ni_route_set_has_source(&DR.routes[0], O(5)),
		 "find fifth source\n");
	unittest(ni_route_set_has_source(&DR.routes[0], O(9)),
		 "find last source\n");
}

void test_ni_route_to_register(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_route_to_register(O(0), O(0), T) < 0,
		 "check for bad route 0-->0\n");
	unittest(ni_route_to_register(O(1), O(0), T) == 1,
		 "validate first destination\n");
	unittest(ni_route_to_register(O(6), O(5), T) == 6,
		 "validate middle destination\n");
	unittest(ni_route_to_register(O(8), O(9), T) == 8,
		 "validate last destination\n");

	/* choice of trigger line in the following is somewhat random */
	unittest(ni_route_to_register(rgout0_src0, TRIGGER_LINE(0), T) == 0,
		 "validate indirect route through rgout0 to TRIGGER_LINE(0)\n");
	unittest(ni_route_to_register(rgout0_src0, TRIGGER_LINE(1), T) == 0,
		 "validate indirect route through rgout0 to TRIGGER_LINE(1)\n");
	unittest(ni_route_to_register(rgout0_src1, TRIGGER_LINE(2), T) == 1,
		 "validate indirect route through rgout0 to TRIGGER_LINE(2)\n");
	unittest(ni_route_to_register(rgout0_src1, TRIGGER_LINE(3), T) == 1,
		 "validate indirect route through rgout0 to TRIGGER_LINE(3)\n");

	unittest(ni_route_to_register(brd0_src0, TRIGGER_LINE(4), T) ==
		 BIT(6),
		 "validate indirect route through brd0 to TRIGGER_LINE(4)\n");
	unittest(ni_route_to_register(brd0_src1, TRIGGER_LINE(4), T) ==
		 BIT(6),
		 "validate indirect route through brd0 to TRIGGER_LINE(4)\n");
	unittest(ni_route_to_register(brd1_src0, TRIGGER_LINE(3), T) ==
		 BIT(6),
		 "validate indirect route through brd1 to TRIGGER_LINE(3)\n");
	unittest(ni_route_to_register(brd1_src1, TRIGGER_LINE(3), T) ==
		 BIT(6),
		 "validate indirect route through brd1 to TRIGGER_LINE(3)\n");
	unittest(ni_route_to_register(brd2_src0, TRIGGER_LINE(2), T) ==
		 BIT(6),
		 "validate indirect route through brd2 to TRIGGER_LINE(2)\n");
	unittest(ni_route_to_register(brd2_src1, TRIGGER_LINE(2), T) ==
		 BIT(6),
		 "validate indirect route through brd2 to TRIGGER_LINE(2)\n");
	unittest(ni_route_to_register(brd3_src0, TRIGGER_LINE(1), T) ==
		 BIT(6),
		 "validate indirect route through brd3 to TRIGGER_LINE(1)\n");
	unittest(ni_route_to_register(brd3_src1, TRIGGER_LINE(1), T) ==
		 BIT(6),
		 "validate indirect route through brd3 to TRIGGER_LINE(1)\n");
}

void test_ni_lookup_route_register(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_lookup_route_register(O(0), O(0), T) == -EINVAL,
		 "check for bad route 0-->0\n");
	unittest(ni_lookup_route_register(O(1), O(0), T) == 1,
		 "validate first destination\n");
	unittest(ni_lookup_route_register(O(6), O(5), T) == 6,
		 "validate middle destination\n");
	unittest(ni_lookup_route_register(O(8), O(9), T) == 8,
		 "validate last destination\n");
	unittest(ni_lookup_route_register(O(10), O(9), T) == -EINVAL,
		 "lookup invalid desination\n");

	unittest(ni_lookup_route_register(rgout0_src0, TRIGGER_LINE(0), T) ==
		 -EINVAL,
		 "rgout0_src0: no direct lookup of indirect route\n");
	unittest(ni_lookup_route_register(rgout0_src0, NI_RGOUT0, T) == 0,
		 "rgout0_src0: lookup indirect route register\n");
	unittest(ni_lookup_route_register(rgout0_src1, TRIGGER_LINE(2), T) ==
		 -EINVAL,
		 "rgout0_src1: no direct lookup of indirect route\n");
	unittest(ni_lookup_route_register(rgout0_src1, NI_RGOUT0, T) == 1,
		 "rgout0_src1: lookup indirect route register\n");

	unittest(ni_lookup_route_register(brd0_src0, TRIGGER_LINE(4), T) ==
		 -EINVAL,
		 "brd0_src0: no direct lookup of indirect route\n");
	unittest(ni_lookup_route_register(brd0_src0, NI_RTSI_BRD(0), T) == 0,
		 "brd0_src0: lookup indirect route register\n");
	unittest(ni_lookup_route_register(brd0_src1, TRIGGER_LINE(4), T) ==
		 -EINVAL,
		 "brd0_src1: no direct lookup of indirect route\n");
	unittest(ni_lookup_route_register(brd0_src1, NI_RTSI_BRD(0), T) == 1,
		 "brd0_src1: lookup indirect route register\n");
}

void test_route_is_valid(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(!route_is_valid(O(0), O(0), T),
		 "check for bad route 0-->0\n");
	unittest(route_is_valid(O(0), O(1), T),
		 "validate first destination\n");
	unittest(route_is_valid(O(5), O(6), T),
		 "validate middle destination\n");
	unittest(route_is_valid(O(8), O(9), T),
		 "validate last destination\n");
}

void test_ni_is_cmd_dest(void)
{
	init_pci_fake();
	unittest(ni_is_cmd_dest(NI_AI_SampleClock),
		 "check that AI/SampleClock is cmd destination\n");
	unittest(ni_is_cmd_dest(NI_AI_StartTrigger),
		 "check that AI/StartTrigger is cmd destination\n");
	unittest(ni_is_cmd_dest(NI_AI_ConvertClock),
		 "check that AI/ConvertClock is cmd destination\n");
	unittest(ni_is_cmd_dest(NI_AO_SampleClock),
		 "check that AO/SampleClock is cmd destination\n");
	unittest(ni_is_cmd_dest(NI_DO_SampleClock),
		 "check that DO/SampleClock is cmd destination\n");
	unittest(!ni_is_cmd_dest(NI_AO_SampleClockTimebase),
		 "check that AO/SampleClockTimebase _not_ cmd destination\n");
}

void test_channel_is_pfi(void)
{
	init_pci_fake();
	unittest(channel_is_pfi(NI_PFI(0)), "check First pfi channel\n");
	unittest(channel_is_pfi(NI_PFI(10)), "check 10th pfi channel\n");
	unittest(channel_is_pfi(NI_PFI(-1)), "check last pfi channel\n");
	unittest(!channel_is_pfi(NI_PFI(-1) + 1),
		 "check first non pfi channel\n");
}

void test_channel_is_rtsi(void)
{
	init_pci_fake();
	unittest(channel_is_rtsi(TRIGGER_LINE(0)),
		 "check First rtsi channel\n");
	unittest(channel_is_rtsi(TRIGGER_LINE(3)),
		 "check 3rd rtsi channel\n");
	unittest(channel_is_rtsi(TRIGGER_LINE(-1)),
		 "check last rtsi channel\n");
	unittest(!channel_is_rtsi(TRIGGER_LINE(-1) + 1),
		 "check first non rtsi channel\n");
}

void test_ni_count_valid_routes(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_count_valid_routes(T) == 57, "count all valid routes\n");
}

void test_ni_get_valid_routes(void)
{
	const struct ni_route_tables *T = &private.routing_tables;
	unsigned int pair_data[2];

	init_pci_fake();
	unittest(ni_get_valid_routes(T, 0, NULL) == 57,
		 "count all valid routes through ni_get_valid_routes\n");

	unittest(ni_get_valid_routes(T, 1, pair_data) == 1,
		 "copied first valid route from ni_get_valid_routes\n");
	unittest(pair_data[0] == O(1),
		 "source of first valid pair from ni_get_valid_routes\n");
	unittest(pair_data[1] == O(0),
		 "destination of first valid pair from ni_get_valid_routes\n");
}

void test_ni_find_route_source(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_find_route_source(4, O(4), T) == -EINVAL,
		 "check for bad source 4-->4\n");
	unittest(ni_find_route_source(0, O(1), T) == O(0),
		 "find first source\n");
	unittest(ni_find_route_source(4, O(6), T) == O(4),
		 "find middle source\n");
	unittest(ni_find_route_source(9, O(8), T) == O(9),
		 "find last source");
	unittest(ni_find_route_source(8, O(9), T) == O(8),
		 "find invalid source (without checking device routes)\n");
}

void test_route_register_is_valid(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(route_register_is_valid(4, O(4), T) == false,
		 "check for bad source 4-->4\n");
	unittest(route_register_is_valid(0, O(1), T) == true,
		 "find first source\n");
	unittest(route_register_is_valid(4, O(6), T) == true,
		 "find middle source\n");
	unittest(route_register_is_valid(9, O(8), T) == true,
		 "find last source");
}

void test_ni_check_trigger_arg(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_check_trigger_arg(0, O(0), T) == -EINVAL,
		 "check bad direct trigger arg for first reg->dest\n");
	unittest(ni_check_trigger_arg(0, O(1), T) == 0,
		 "check direct trigger arg for first reg->dest\n");
	unittest(ni_check_trigger_arg(4, O(6), T) == 0,
		 "check direct trigger arg for middle reg->dest\n");
	unittest(ni_check_trigger_arg(9, O(8), T) == 0,
		 "check direct trigger arg for last reg->dest\n");

	unittest(ni_check_trigger_arg_roffs(-1, O(0), T, 1) == -EINVAL,
		 "check bad direct trigger arg for first reg->dest w/offs\n");
	unittest(ni_check_trigger_arg_roffs(0, O(1), T, 0) == 0,
		 "check direct trigger arg for first reg->dest w/offs\n");
	unittest(ni_check_trigger_arg_roffs(3, O(6), T, 1) == 0,
		 "check direct trigger arg for middle reg->dest w/offs\n");
	unittest(ni_check_trigger_arg_roffs(7, O(8), T, 2) == 0,
		 "check direct trigger arg for last reg->dest w/offs\n");

	unittest(ni_check_trigger_arg(O(0), O(0), T) == -EINVAL,
		 "check bad trigger arg for first src->dest\n");
	unittest(ni_check_trigger_arg(O(0), O(1), T) == 0,
		 "check trigger arg for first src->dest\n");
	unittest(ni_check_trigger_arg(O(5), O(6), T) == 0,
		 "check trigger arg for middle src->dest\n");
	unittest(ni_check_trigger_arg(O(8), O(9), T) == 0,
		 "check trigger arg for last src->dest\n");
}

void test_ni_get_reg_value(void)
{
	const struct ni_route_tables *T = &private.routing_tables;

	init_pci_fake();
	unittest(ni_get_reg_value(0, O(0), T) == -1,
		 "check bad direct trigger arg for first reg->dest\n");
	unittest(ni_get_reg_value(0, O(1), T) == 0,
		 "check direct trigger arg for first reg->dest\n");
	unittest(ni_get_reg_value(4, O(6), T) == 4,
		 "check direct trigger arg for middle reg->dest\n");
	unittest(ni_get_reg_value(9, O(8), T) == 9,
		 "check direct trigger arg for last reg->dest\n");

	unittest(ni_get_reg_value_roffs(-1, O(0), T, 1) == -1,
		 "check bad direct trigger arg for first reg->dest w/offs\n");
	unittest(ni_get_reg_value_roffs(0, O(1), T, 0) == 0,
		 "check direct trigger arg for first reg->dest w/offs\n");
	unittest(ni_get_reg_value_roffs(3, O(6), T, 1) == 4,
		 "check direct trigger arg for middle reg->dest w/offs\n");
	unittest(ni_get_reg_value_roffs(7, O(8), T, 2) == 9,
		 "check direct trigger arg for last reg->dest w/offs\n");

	unittest(ni_get_reg_value(O(0), O(0), T) == -1,
		 "check bad trigger arg for first src->dest\n");
	unittest(ni_get_reg_value(O(0), O(1), T) == 0,
		 "check trigger arg for first src->dest\n");
	unittest(ni_get_reg_value(O(5), O(6), T) == 5,
		 "check trigger arg for middle src->dest\n");
	unittest(ni_get_reg_value(O(8), O(9), T) == 8,
		 "check trigger arg for last src->dest\n");
}

/* **** BEGIN simple module entry/exit functions **** */
static int __init ni_routes_unittest(void)
{
	const unittest_fptr unit_tests[] = {
		(unittest_fptr)test_ni_assign_device_routes,
		(unittest_fptr)test_ni_sort_device_routes,
		(unittest_fptr)test_ni_find_route_set,
		(unittest_fptr)test_ni_route_set_has_source,
		(unittest_fptr)test_ni_route_to_register,
		(unittest_fptr)test_ni_lookup_route_register,
		(unittest_fptr)test_route_is_valid,
		(unittest_fptr)test_ni_is_cmd_dest,
		(unittest_fptr)test_channel_is_pfi,
		(unittest_fptr)test_channel_is_rtsi,
		(unittest_fptr)test_ni_count_valid_routes,
		(unittest_fptr)test_ni_get_valid_routes,
		(unittest_fptr)test_ni_find_route_source,
		(unittest_fptr)test_route_register_is_valid,
		(unittest_fptr)test_ni_check_trigger_arg,
		(unittest_fptr)test_ni_get_reg_value,
		NULL,
	};

	exec_unittests("ni_routes", unit_tests);
	return 0;
}

static void __exit ni_routes_unittest_exit(void) { }

module_init(ni_routes_unittest);
module_exit(ni_routes_unittest_exit);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi unit-tests for ni_routes module");
MODULE_LICENSE("GPL");
/* **** END simple module entry/exit functions **** */

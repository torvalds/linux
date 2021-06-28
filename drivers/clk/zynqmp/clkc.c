// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016-2019 Xilinx
 *
 * Based on drivers/clk/zynq/clkc.c
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "clk-zynqmp.h"

#define MAX_PARENT			100
#define MAX_NODES			6
#define MAX_NAME_LEN			50

/* Flags for parents */
#define PARENT_CLK_SELF			0
#define PARENT_CLK_NODE1		1
#define PARENT_CLK_NODE2		2
#define PARENT_CLK_NODE3		3
#define PARENT_CLK_NODE4		4
#define PARENT_CLK_EXTERNAL		5

#define END_OF_CLK_NAME			"END_OF_CLK"
#define END_OF_TOPOLOGY_NODE		1
#define END_OF_PARENTS			1
#define RESERVED_CLK_NAME		""

#define CLK_GET_NAME_RESP_LEN		16
#define CLK_GET_TOPOLOGY_RESP_WORDS	3
#define CLK_GET_PARENTS_RESP_WORDS	3
#define CLK_GET_ATTR_RESP_WORDS		1

enum clk_type {
	CLK_TYPE_OUTPUT,
	CLK_TYPE_EXTERNAL,
};

/**
 * struct clock_parent - Clock parent
 * @name:	Parent name
 * @id:		Parent clock ID
 * @flag:	Parent flags
 */
struct clock_parent {
	char name[MAX_NAME_LEN];
	int id;
	u32 flag;
};

/**
 * struct zynqmp_clock - Clock
 * @clk_name:		Clock name
 * @valid:		Validity flag of clock
 * @type:		Clock type (Output/External)
 * @node:		Clock topology nodes
 * @num_nodes:		Number of nodes present in topology
 * @parent:		Parent of clock
 * @num_parents:	Number of parents of clock
 * @clk_id:		Clock id
 */
struct zynqmp_clock {
	char clk_name[MAX_NAME_LEN];
	u32 valid;
	enum clk_type type;
	struct clock_topology node[MAX_NODES];
	u32 num_nodes;
	struct clock_parent parent[MAX_PARENT];
	u32 num_parents;
	u32 clk_id;
};

struct name_resp {
	char name[CLK_GET_NAME_RESP_LEN];
};

struct topology_resp {
#define CLK_TOPOLOGY_TYPE		GENMASK(3, 0)
#define CLK_TOPOLOGY_CUSTOM_TYPE_FLAGS	GENMASK(7, 4)
#define CLK_TOPOLOGY_FLAGS		GENMASK(23, 8)
#define CLK_TOPOLOGY_TYPE_FLAGS		GENMASK(31, 24)
	u32 topology[CLK_GET_TOPOLOGY_RESP_WORDS];
};

struct parents_resp {
#define NA_PARENT			0xFFFFFFFF
#define DUMMY_PARENT			0xFFFFFFFE
#define CLK_PARENTS_ID			GENMASK(15, 0)
#define CLK_PARENTS_FLAGS		GENMASK(31, 16)
	u32 parents[CLK_GET_PARENTS_RESP_WORDS];
};

struct attr_resp {
#define CLK_ATTR_VALID			BIT(0)
#define CLK_ATTR_TYPE			BIT(2)
#define CLK_ATTR_NODE_INDEX		GENMASK(13, 0)
#define CLK_ATTR_NODE_TYPE		GENMASK(19, 14)
#define CLK_ATTR_NODE_SUBCLASS		GENMASK(25, 20)
#define CLK_ATTR_NODE_CLASS		GENMASK(31, 26)
	u32 attr[CLK_GET_ATTR_RESP_WORDS];
};

static const char clk_type_postfix[][10] = {
	[TYPE_INVALID] = "",
	[TYPE_MUX] = "_mux",
	[TYPE_GATE] = "",
	[TYPE_DIV1] = "_div1",
	[TYPE_DIV2] = "_div2",
	[TYPE_FIXEDFACTOR] = "_ff",
	[TYPE_PLL] = ""
};

static struct clk_hw *(* const clk_topology[]) (const char *name, u32 clk_id,
					const char * const *parents,
					u8 num_parents,
					const struct clock_topology *nodes)
					= {
	[TYPE_INVALID] = NULL,
	[TYPE_MUX] = zynqmp_clk_register_mux,
	[TYPE_PLL] = zynqmp_clk_register_pll,
	[TYPE_FIXEDFACTOR] = zynqmp_clk_register_fixed_factor,
	[TYPE_DIV1] = zynqmp_clk_register_divider,
	[TYPE_DIV2] = zynqmp_clk_register_divider,
	[TYPE_GATE] = zynqmp_clk_register_gate
};

static struct zynqmp_clock *clock;
static struct clk_hw_onecell_data *zynqmp_data;
static unsigned int clock_max_idx;

/**
 * zynqmp_is_valid_clock() - Check whether clock is valid or not
 * @clk_id:	Clock index
 *
 * Return: 1 if clock is valid, 0 if clock is invalid else error code
 */
static inline int zynqmp_is_valid_clock(u32 clk_id)
{
	if (clk_id >= clock_max_idx)
		return -ENODEV;

	return clock[clk_id].valid;
}

/**
 * zynqmp_get_clock_name() - Get name of clock from Clock index
 * @clk_id:	Clock index
 * @clk_name:	Name of clock
 *
 * Return: 0 on success else error code
 */
static int zynqmp_get_clock_name(u32 clk_id, char *clk_name)
{
	int ret;

	ret = zynqmp_is_valid_clock(clk_id);
	if (ret == 1) {
		strncpy(clk_name, clock[clk_id].clk_name, MAX_NAME_LEN);
		return 0;
	}

	return ret == 0 ? -EINVAL : ret;
}

/**
 * zynqmp_get_clock_type() - Get type of clock
 * @clk_id:	Clock index
 * @type:	Clock type: CLK_TYPE_OUTPUT or CLK_TYPE_EXTERNAL
 *
 * Return: 0 on success else error code
 */
static int zynqmp_get_clock_type(u32 clk_id, u32 *type)
{
	int ret;

	ret = zynqmp_is_valid_clock(clk_id);
	if (ret == 1) {
		*type = clock[clk_id].type;
		return 0;
	}

	return ret == 0 ? -EINVAL : ret;
}

/**
 * zynqmp_pm_clock_get_num_clocks() - Get number of clocks in system
 * @nclocks:	Number of clocks in system/board.
 *
 * Call firmware API to get number of clocks.
 *
 * Return: 0 on success else error code.
 */
static int zynqmp_pm_clock_get_num_clocks(u32 *nclocks)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_NUM_CLOCKS;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	*nclocks = ret_payload[1];

	return ret;
}

/**
 * zynqmp_pm_clock_get_name() - Get the name of clock for given id
 * @clock_id:	ID of the clock to be queried
 * @response:	Name of the clock with the given id
 *
 * This function is used to get name of clock specified by given
 * clock ID.
 *
 * Return: Returns 0
 */
static int zynqmp_pm_clock_get_name(u32 clock_id,
				    struct name_resp *response)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];

	qdata.qid = PM_QID_CLOCK_GET_NAME;
	qdata.arg1 = clock_id;

	zynqmp_pm_query_data(qdata, ret_payload);
	memcpy(response, ret_payload, sizeof(*response));

	return 0;
}

/**
 * zynqmp_pm_clock_get_topology() - Get the topology of clock for given id
 * @clock_id:	ID of the clock to be queried
 * @index:	Node index of clock topology
 * @response:	Buffer used for the topology response
 *
 * This function is used to get topology information for the clock
 * specified by given clock ID.
 *
 * This API will return 3 node of topology with a single response. To get
 * other nodes, master should call same API in loop with new
 * index till error is returned. E.g First call should have
 * index 0 which will return nodes 0,1 and 2. Next call, index
 * should be 3 which will return nodes 3,4 and 5 and so on.
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_pm_clock_get_topology(u32 clock_id, u32 index,
					struct topology_resp *response)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_TOPOLOGY;
	qdata.arg1 = clock_id;
	qdata.arg2 = index;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	memcpy(response, &ret_payload[1], sizeof(*response));

	return ret;
}

unsigned long zynqmp_clk_map_common_ccf_flags(const u32 zynqmp_flag)
{
	unsigned long ccf_flag = 0;

	if (zynqmp_flag & ZYNQMP_CLK_SET_RATE_GATE)
		ccf_flag |= CLK_SET_RATE_GATE;
	if (zynqmp_flag & ZYNQMP_CLK_SET_PARENT_GATE)
		ccf_flag |= CLK_SET_PARENT_GATE;
	if (zynqmp_flag & ZYNQMP_CLK_SET_RATE_PARENT)
		ccf_flag |= CLK_SET_RATE_PARENT;
	if (zynqmp_flag & ZYNQMP_CLK_IGNORE_UNUSED)
		ccf_flag |= CLK_IGNORE_UNUSED;
	if (zynqmp_flag & ZYNQMP_CLK_SET_RATE_NO_REPARENT)
		ccf_flag |= CLK_SET_RATE_NO_REPARENT;
	if (zynqmp_flag & ZYNQMP_CLK_IS_CRITICAL)
		ccf_flag |= CLK_IS_CRITICAL;

	return ccf_flag;
}

/**
 * zynqmp_clk_register_fixed_factor() - Register fixed factor with the
 *					clock framework
 * @name:		Name of this clock
 * @clk_id:		Clock ID
 * @parents:		Name of this clock's parents
 * @num_parents:	Number of parents
 * @nodes:		Clock topology node
 *
 * Return: clock hardware to the registered clock
 */
struct clk_hw *zynqmp_clk_register_fixed_factor(const char *name, u32 clk_id,
					const char * const *parents,
					u8 num_parents,
					const struct clock_topology *nodes)
{
	u32 mult, div;
	struct clk_hw *hw;
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;
	unsigned long flag;

	qdata.qid = PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS;
	qdata.arg1 = clk_id;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	if (ret)
		return ERR_PTR(ret);

	mult = ret_payload[1];
	div = ret_payload[2];

	flag = zynqmp_clk_map_common_ccf_flags(nodes->flag);

	hw = clk_hw_register_fixed_factor(NULL, name,
					  parents[0],
					  flag, mult,
					  div);

	return hw;
}

/**
 * zynqmp_pm_clock_get_parents() - Get the first 3 parents of clock for given id
 * @clock_id:	Clock ID
 * @index:	Parent index
 * @response:	Parents of the given clock
 *
 * This function is used to get 3 parents for the clock specified by
 * given clock ID.
 *
 * This API will return 3 parents with a single response. To get
 * other parents, master should call same API in loop with new
 * parent index till error is returned. E.g First call should have
 * index 0 which will return parents 0,1 and 2. Next call, index
 * should be 3 which will return parent 3,4 and 5 and so on.
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_pm_clock_get_parents(u32 clock_id, u32 index,
				       struct parents_resp *response)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_PARENTS;
	qdata.arg1 = clock_id;
	qdata.arg2 = index;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	memcpy(response, &ret_payload[1], sizeof(*response));

	return ret;
}

/**
 * zynqmp_pm_clock_get_attributes() - Get the attributes of clock for given id
 * @clock_id:	Clock ID
 * @response:	Clock attributes response
 *
 * This function is used to get clock's attributes(e.g. valid, clock type, etc).
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_pm_clock_get_attributes(u32 clock_id,
					  struct attr_resp *response)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_ATTRIBUTES;
	qdata.arg1 = clock_id;

	ret = zynqmp_pm_query_data(qdata, ret_payload);
	memcpy(response, &ret_payload[1], sizeof(*response));

	return ret;
}

/**
 * __zynqmp_clock_get_topology() - Get topology data of clock from firmware
 *				   response data
 * @topology:		Clock topology
 * @response:		Clock topology data received from firmware
 * @nnodes:		Number of nodes
 *
 * Return: 0 on success else error+reason
 */
static int __zynqmp_clock_get_topology(struct clock_topology *topology,
				       struct topology_resp *response,
				       u32 *nnodes)
{
	int i;
	u32 type;

	for (i = 0; i < ARRAY_SIZE(response->topology); i++) {
		type = FIELD_GET(CLK_TOPOLOGY_TYPE, response->topology[i]);
		if (type == TYPE_INVALID)
			return END_OF_TOPOLOGY_NODE;
		topology[*nnodes].type = type;
		topology[*nnodes].flag = FIELD_GET(CLK_TOPOLOGY_FLAGS,
						   response->topology[i]);
		topology[*nnodes].type_flag =
				FIELD_GET(CLK_TOPOLOGY_TYPE_FLAGS,
					  response->topology[i]);
		topology[*nnodes].custom_type_flag =
			FIELD_GET(CLK_TOPOLOGY_CUSTOM_TYPE_FLAGS,
				  response->topology[i]);
		(*nnodes)++;
	}

	return 0;
}

/**
 * zynqmp_clock_get_topology() - Get topology of clock from firmware using
 *				 PM_API
 * @clk_id:		Clock index
 * @topology:		Clock topology
 * @num_nodes:		Number of nodes
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_clock_get_topology(u32 clk_id,
				     struct clock_topology *topology,
				     u32 *num_nodes)
{
	int j, ret;
	struct topology_resp response = { };

	*num_nodes = 0;
	for (j = 0; j <= MAX_NODES; j += ARRAY_SIZE(response.topology)) {
		ret = zynqmp_pm_clock_get_topology(clock[clk_id].clk_id, j,
						   &response);
		if (ret)
			return ret;
		ret = __zynqmp_clock_get_topology(topology, &response,
						  num_nodes);
		if (ret == END_OF_TOPOLOGY_NODE)
			return 0;
	}

	return 0;
}

/**
 * __zynqmp_clock_get_parents() - Get parents info of clock from firmware
 *				   response data
 * @parents:		Clock parents
 * @response:		Clock parents data received from firmware
 * @nparent:		Number of parent
 *
 * Return: 0 on success else error+reason
 */
static int __zynqmp_clock_get_parents(struct clock_parent *parents,
				      struct parents_resp *response,
				      u32 *nparent)
{
	int i;
	struct clock_parent *parent;

	for (i = 0; i < ARRAY_SIZE(response->parents); i++) {
		if (response->parents[i] == NA_PARENT)
			return END_OF_PARENTS;

		parent = &parents[i];
		parent->id = FIELD_GET(CLK_PARENTS_ID, response->parents[i]);
		if (response->parents[i] == DUMMY_PARENT) {
			strcpy(parent->name, "dummy_name");
			parent->flag = 0;
		} else {
			parent->flag = FIELD_GET(CLK_PARENTS_FLAGS,
						 response->parents[i]);
			if (zynqmp_get_clock_name(parent->id, parent->name))
				continue;
		}
		*nparent += 1;
	}

	return 0;
}

/**
 * zynqmp_clock_get_parents() - Get parents info from firmware using PM_API
 * @clk_id:		Clock index
 * @parents:		Clock parents
 * @num_parents:	Total number of parents
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_clock_get_parents(u32 clk_id, struct clock_parent *parents,
				    u32 *num_parents)
{
	int j = 0, ret;
	struct parents_resp response = { };

	*num_parents = 0;
	do {
		/* Get parents from firmware */
		ret = zynqmp_pm_clock_get_parents(clock[clk_id].clk_id, j,
						  &response);
		if (ret)
			return ret;

		ret = __zynqmp_clock_get_parents(&parents[j], &response,
						 num_parents);
		if (ret == END_OF_PARENTS)
			return 0;
		j += ARRAY_SIZE(response.parents);
	} while (*num_parents <= MAX_PARENT);

	return 0;
}

/**
 * zynqmp_get_parent_list() - Create list of parents name
 * @np:			Device node
 * @clk_id:		Clock index
 * @parent_list:	List of parent's name
 * @num_parents:	Total number of parents
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_get_parent_list(struct device_node *np, u32 clk_id,
				  const char **parent_list, u32 *num_parents)
{
	int i = 0, ret;
	u32 total_parents = clock[clk_id].num_parents;
	struct clock_topology *clk_nodes;
	struct clock_parent *parents;

	clk_nodes = clock[clk_id].node;
	parents = clock[clk_id].parent;

	for (i = 0; i < total_parents; i++) {
		if (!parents[i].flag) {
			parent_list[i] = parents[i].name;
		} else if (parents[i].flag == PARENT_CLK_EXTERNAL) {
			ret = of_property_match_string(np, "clock-names",
						       parents[i].name);
			if (ret < 0)
				strcpy(parents[i].name, "dummy_name");
			parent_list[i] = parents[i].name;
		} else {
			strcat(parents[i].name,
			       clk_type_postfix[clk_nodes[parents[i].flag - 1].
			       type]);
			parent_list[i] = parents[i].name;
		}
	}

	*num_parents = total_parents;
	return 0;
}

/**
 * zynqmp_register_clk_topology() - Register clock topology
 * @clk_id:		Clock index
 * @clk_name:		Clock Name
 * @num_parents:	Total number of parents
 * @parent_names:	List of parents name
 *
 * Return: Returns either clock hardware or error+reason
 */
static struct clk_hw *zynqmp_register_clk_topology(int clk_id, char *clk_name,
						   int num_parents,
						   const char **parent_names)
{
	int j;
	u32 num_nodes, clk_dev_id;
	char *clk_out[MAX_NODES];
	struct clock_topology *nodes;
	struct clk_hw *hw = NULL;

	nodes = clock[clk_id].node;
	num_nodes = clock[clk_id].num_nodes;
	clk_dev_id = clock[clk_id].clk_id;

	for (j = 0; j < num_nodes; j++) {
		/*
		 * Clock name received from firmware is output clock name.
		 * Intermediate clock names are postfixed with type of clock.
		 */
		if (j != (num_nodes - 1)) {
			clk_out[j] = kasprintf(GFP_KERNEL, "%s%s", clk_name,
					    clk_type_postfix[nodes[j].type]);
		} else {
			clk_out[j] = kasprintf(GFP_KERNEL, "%s", clk_name);
		}

		if (!clk_topology[nodes[j].type])
			continue;

		hw = (*clk_topology[nodes[j].type])(clk_out[j], clk_dev_id,
						    parent_names,
						    num_parents,
						    &nodes[j]);
		if (IS_ERR(hw))
			pr_warn_once("%s() 0x%x: %s register fail with %ld\n",
				     __func__,  clk_dev_id, clk_name,
				     PTR_ERR(hw));

		parent_names[0] = clk_out[j];
	}

	for (j = 0; j < num_nodes; j++)
		kfree(clk_out[j]);

	return hw;
}

/**
 * zynqmp_register_clocks() - Register clocks
 * @np:		Device node
 *
 * Return: 0 on success else error code
 */
static int zynqmp_register_clocks(struct device_node *np)
{
	int ret;
	u32 i, total_parents = 0, type = 0;
	const char *parent_names[MAX_PARENT];

	for (i = 0; i < clock_max_idx; i++) {
		char clk_name[MAX_NAME_LEN];

		/* get clock name, continue to next clock if name not found */
		if (zynqmp_get_clock_name(i, clk_name))
			continue;

		/* Check if clock is valid and output clock.
		 * Do not register invalid or external clock.
		 */
		ret = zynqmp_get_clock_type(i, &type);
		if (ret || type != CLK_TYPE_OUTPUT)
			continue;

		/* Get parents of clock*/
		if (zynqmp_get_parent_list(np, i, parent_names,
					   &total_parents)) {
			WARN_ONCE(1, "No parents found for %s\n",
				  clock[i].clk_name);
			continue;
		}

		zynqmp_data->hws[i] =
			zynqmp_register_clk_topology(i, clk_name,
						     total_parents,
						     parent_names);
	}

	for (i = 0; i < clock_max_idx; i++) {
		if (IS_ERR(zynqmp_data->hws[i])) {
			pr_err("Zynq Ultrascale+ MPSoC clk %s: register failed with %ld\n",
			       clock[i].clk_name, PTR_ERR(zynqmp_data->hws[i]));
			WARN_ON(1);
		}
	}
	return 0;
}

/**
 * zynqmp_get_clock_info() - Get clock information from firmware using PM_API
 */
static void zynqmp_get_clock_info(void)
{
	int i, ret;
	u32 type = 0;
	u32 nodetype, subclass, class;
	struct attr_resp attr;
	struct name_resp name;

	for (i = 0; i < clock_max_idx; i++) {
		ret = zynqmp_pm_clock_get_attributes(i, &attr);
		if (ret)
			continue;

		clock[i].valid = FIELD_GET(CLK_ATTR_VALID, attr.attr[0]);
		/* skip query for Invalid clock */
		ret = zynqmp_is_valid_clock(i);
		if (ret != CLK_ATTR_VALID)
			continue;

		clock[i].type = FIELD_GET(CLK_ATTR_TYPE, attr.attr[0]) ?
			CLK_TYPE_EXTERNAL : CLK_TYPE_OUTPUT;

		nodetype = FIELD_GET(CLK_ATTR_NODE_TYPE, attr.attr[0]);
		subclass = FIELD_GET(CLK_ATTR_NODE_SUBCLASS, attr.attr[0]);
		class = FIELD_GET(CLK_ATTR_NODE_CLASS, attr.attr[0]);

		clock[i].clk_id = FIELD_PREP(CLK_ATTR_NODE_CLASS, class) |
				  FIELD_PREP(CLK_ATTR_NODE_SUBCLASS, subclass) |
				  FIELD_PREP(CLK_ATTR_NODE_TYPE, nodetype) |
				  FIELD_PREP(CLK_ATTR_NODE_INDEX, i);

		zynqmp_pm_clock_get_name(clock[i].clk_id, &name);
		if (!strcmp(name.name, RESERVED_CLK_NAME))
			continue;
		strncpy(clock[i].clk_name, name.name, MAX_NAME_LEN);
	}

	/* Get topology of all clock */
	for (i = 0; i < clock_max_idx; i++) {
		ret = zynqmp_get_clock_type(i, &type);
		if (ret || type != CLK_TYPE_OUTPUT)
			continue;

		ret = zynqmp_clock_get_topology(i, clock[i].node,
						&clock[i].num_nodes);
		if (ret)
			continue;

		ret = zynqmp_clock_get_parents(i, clock[i].parent,
					       &clock[i].num_parents);
		if (ret)
			continue;
	}
}

/**
 * zynqmp_clk_setup() - Setup the clock framework and register clocks
 * @np:		Device node
 *
 * Return: 0 on success else error code
 */
static int zynqmp_clk_setup(struct device_node *np)
{
	int ret;

	ret = zynqmp_pm_clock_get_num_clocks(&clock_max_idx);
	if (ret)
		return ret;

	zynqmp_data = kzalloc(struct_size(zynqmp_data, hws, clock_max_idx),
			      GFP_KERNEL);
	if (!zynqmp_data)
		return -ENOMEM;

	clock = kcalloc(clock_max_idx, sizeof(*clock), GFP_KERNEL);
	if (!clock) {
		kfree(zynqmp_data);
		return -ENOMEM;
	}

	zynqmp_get_clock_info();
	zynqmp_register_clocks(np);

	zynqmp_data->num = clock_max_idx;
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, zynqmp_data);

	return 0;
}

static int zynqmp_clock_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	ret = zynqmp_clk_setup(dev->of_node);

	return ret;
}

static const struct of_device_id zynqmp_clock_of_match[] = {
	{.compatible = "xlnx,zynqmp-clk"},
	{.compatible = "xlnx,versal-clk"},
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_clock_of_match);

static struct platform_driver zynqmp_clock_driver = {
	.driver = {
		.name = "zynqmp_clock",
		.of_match_table = zynqmp_clock_of_match,
	},
	.probe = zynqmp_clock_probe,
};
module_platform_driver(zynqmp_clock_driver);

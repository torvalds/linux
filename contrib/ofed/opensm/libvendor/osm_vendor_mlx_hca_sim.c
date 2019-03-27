/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#if defined(OSM_VENDOR_INTF_SIM)
#undef IN
#undef OUT

#include <unistd.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_log.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>

/******************************************************************************
*
* Provides the functionality for selecting an HCA Port and Obtaining it's guid.
* This version is based on $IBMGTSIM_DIR/$IBMGTSIM_NODE file system.
* This is a mimic of the OpenIB gen1 file system
*
******************************************************************************/

char *__get_simulator_dir(void)
{
	static char *ibmgtSimDir = NULL;
	static char *defaultIbmgtSimDir = "/tmp/ibmgtsim";
	static char *ibmgtSimNode = NULL;
	static char dirName[1024];

	/* we use the first pointer to know if we were here */
	if (ibmgtSimDir == NULL) {
		/* obtain the simulator directory */
		ibmgtSimDir = getenv("IBMGTSIM_DIR");
		if (ibmgtSimDir == NULL) {
			printf
			    ("-W- Environment variable: IBMGTSIM_DIR does not exist.\n");
			printf
			    ("    Please create one used by the simulator.\n");
			printf("    Using /tmp/ibmgtsim as default.\n");
			ibmgtSimDir = defaultIbmgtSimDir;
		}

		/* obtain the node name we simulate */
		ibmgtSimNode = getenv("IBMGTSIM_NODE");
		if (ibmgtSimNode == NULL) {
			printf
			    ("-W- Environment variable: IBMGTSIM_NODE does not exist.\n");
			printf
			    ("    This variable should be the name of the node you wish to simulate.\n");
			printf("    Using H-1 as default.\n");
			ibmgtSimNode = "H-1";
		}
		sprintf(dirName, "%s/%s", ibmgtSimDir, ibmgtSimNode);
	}

	return dirName;
}

typedef struct _osm_ca_info {
	ib_net64_t guid;
	size_t attr_size;
	ib_ca_attr_t *p_attr;

} osm_ca_info_t;

/**********************************************************************
 * Returns a pointer to the port attribute of the specified port
 * owned by this CA.
 ************************************************************************/
static ib_port_attr_t *__osm_ca_info_get_port_attr_ptr(IN const osm_ca_info_t *
						       const p_ca_info,
						       IN const uint8_t index)
{
	return (&p_ca_info->p_attr->p_port_attr[index]);
}

/**********************************************************************
 * Obtain the number of local CAs by scanning /proc/infiniband/core
 **********************************************************************/
int __hca_sim_get_num_cas(void)
{
	int num_cas = 0;
	DIR *dp;
	struct dirent *ep;

	dp = opendir(__get_simulator_dir());

	if (dp != NULL) {
		while ((ep = readdir(dp))) {
			/* CAs are directories with the format ca[1-9][0-9]* */
			/*  if ((ep->d_type == DT_DIR) && !strncmp(ep->d_name, "ca", 2)) */
			if (!strncmp(ep->d_name, "ca", 2)) {
				num_cas++;
			}
		}
		closedir(dp);
	} else {
		printf("__hca_sim_get_num_cas: ERROR : ail to open dir %s\n",
		       __get_simulator_dir());
		exit(1);
	}

	if (!num_cas)
		exit(1);
	return num_cas;
}

/*
  name:          InfiniHost0
  provider:      tavor
  node GUID:     0002:c900:0120:3470
  ports:         2
  vendor ID:     0x2c9
  device ID:     0x5a44
  HW revision:   0xa1
  FW revision:   0x300020080
*/
typedef struct _sim_ca_info {
	char name[32];
	char provider[32];
	uint64_t guid;
	uint8_t num_ports;
	uint32_t vend_id;
	uint16_t dev_id;
	uint16_t rev_id;
	uint64_t fw_rev;
} sim_ca_info_t;

/**********************************************************************
 * Parse the CA Info file available in ibmgtSimDir/caN/info
 **********************************************************************/
static ib_api_status_t
__parse_ca_info_file(IN osm_vendor_t * const p_vend,
		     IN uint32_t idx, OUT sim_ca_info_t * sim_ca_info)
{
	ib_api_status_t status = IB_ERROR;
	int info_file;
	char file_name[256];
	char file_buffer[3200];
	char *p_ch;
	int g1, g2, g3, g4;
	int num_ports;
	uint32_t len;

	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__parse_ca_info_file: " "Querying CA %d.\n", idx);

	/* we use the proc file system so we must be able to open the info file .. */
	sprintf(file_name, "%s/ca%d/info", __get_simulator_dir(), idx);
	info_file = open(file_name, O_RDONLY);
	if (!info_file) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5105: "
			"Fail to open HCA:%d info file:(%s).\n", idx,
			file_name);
		goto Exit;
	}

	/* read in the file */
	len = read(info_file, file_buffer, 3200);
	close(info_file);
	file_buffer[len] = '\0';

	/*
	   parse the file ...
	   name:          InfiniHost0
	   provider:      tavor
	   node GUID:     0002:c900:0120:3470
	   ports:         2
	   vendor ID:     0x2c9
	   device ID:     0x5a44
	   HW revision:   0xa1
	   FW revision:   0x300020080
	 */
	if (!(p_ch = strstr(file_buffer, "name:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5106: "
			"Fail to obtain HCA name. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "name: %s", sim_ca_info->name) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5107: "
			"Fail to parse name in info file:(%s).\n", p_ch);
		goto Exit;
	}

	/* get the guid of the HCA */
	if (!(p_ch = strstr(file_buffer, "node GUID:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5108: "
			"Fail to obtain GUID in info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "node GUID: %x:%x:%x:%x", &g1, &g2, &g3, &g4) != 4) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5109: "
			"Fail to parse GUID in info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_ca_info->guid = (uint64_t) g1 << 48 | (uint64_t) g1 << 32
	    | (uint64_t) g1 << 16 | (uint64_t) g3;

	/* obtain number of ports */
	if (!(p_ch = strstr(file_buffer, "ports:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5110: "
			"Fail to obtain number of ports in info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "ports: %d", &num_ports) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_ca_info_file: ERR 5111: "
			"Fail to parse num ports in info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_ca_info->num_ports = num_ports;

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__parse_ca_info_file: "
		"CA1 = name:%s guid:0x%" PRIx64 " ports:%d\n",
		sim_ca_info->name, sim_ca_info->guid, sim_ca_info->num_ports);

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return status;
}

/*
  state:         ACTIVE
  LID:           0x0001
  LMC:           0x0000
  SM LID:        0x0001
  SM SL:         0x0000
  Capabilities:  IsSM
  IsTrapSupported
  IsAutomaticMigrationSupported
  IsSLMappingSupported
  IsLEDInfoSupported
  IsSystemImageGUIDSupported
  IsVendorClassSupported
  IsCapabilityMaskNoticeSupported
*/
typedef struct _sim_port_info {
	uint8_t state;
	uint16_t lid;
	uint8_t lmc;
	uint16_t sm_lid;
	uint8_t sm_sl;
} sim_port_info_t;

/**********************************************************************
 * Parse the Port Info file available in ibmgtSimDir/caN/portM/info
 * Port num is 1..N
 **********************************************************************/
static ib_api_status_t
__parse_port_info_file(IN osm_vendor_t * const p_vend,
		       IN uint32_t hca_idx,
		       IN uint8_t port_num, OUT sim_port_info_t * sim_port_info)
{
	ib_api_status_t status = IB_ERROR;
	int info_file;
	char file_name[256];
	char file_buffer[3200];
	char state[12];
	char *p_ch;
	int lid, sm_lid, lmc, sm_sl;
	uint32_t len;

	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__parse_port_info_file: "
		"Parsing Proc File System Port Info CA %d Port %d.\n", hca_idx,
		port_num);

	/* we use the proc file system so we must be able to open the info file .. */
	sprintf(file_name, "%s/ca%d/port%d/info", __get_simulator_dir(),
		hca_idx, port_num);
	info_file = open(file_name, O_RDONLY);
	if (!info_file) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5112: "
			"Fail to open HCA:%d Port:%d info file:(%s).\n",
			hca_idx, port_num, file_name);
		goto Exit;
	}

	/* read in the file */
	len = read(info_file, file_buffer, 3200);
	close(info_file);
	file_buffer[len] = '\0';

	/*
	   parse the file ...
	   state:         ACTIVE
	   LID:           0x0001
	   LMC:           0x0000
	   SM LID:        0x0001
	   SM SL:         0x0000
	   ...
	 */
	if (!(p_ch = strstr(file_buffer, "state:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5113: "
			"Fail to obtain port state. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "state: %s", state) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5114: "
			"Fail to parse state from info file:(%s).\n", p_ch);
		goto Exit;
	}

	if (!strcmp(state, "ACTIVE"))
		sim_port_info->state = IB_LINK_ACTIVE;
	else if (!strcmp(state, "DOWN"))
		sim_port_info->state = IB_LINK_DOWN;
	else if (!strcmp(state, "INIT"))
		sim_port_info->state = IB_LINK_INIT;
	else if (!strcmp(state, "ARMED"))
		sim_port_info->state = IB_LINK_ARMED;
	else
		sim_port_info->state = 0;

	/* get lid */
	if (!(p_ch = strstr(file_buffer, "LID:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5115: "
			"Fail to obtain port lid. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "LID: %x", &lid) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5116: "
			"Fail to parse lid from info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_port_info->lid = lid;
	/* get LMC */
	if (!(p_ch = strstr(file_buffer, "LMC:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5117: "
			"Fail to obtain port LMC. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "LMC: %x", &lmc) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5118: "
			"Fail to parse LMC from info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_port_info->lmc = lmc;

	/* get SM LID */
	if (!(p_ch = strstr(file_buffer, "SM LID:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5119: "
			"Fail to obtain port SM LID. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "SM LID: %x", &sm_lid) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5120: "
			"Fail to parse SM LID from info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_port_info->sm_lid = sm_lid;

	/* get SM LID */
	if (!(p_ch = strstr(file_buffer, "SM SL:"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5121: "
			"Fail to obtain port SM SL. In info file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch, "SM SL: %x", &sm_sl) != 1) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__parse_port_info_file: ERR 5122: "
			"Fail to parse SM SL from info file:(%s).\n", p_ch);
		goto Exit;
	}
	sim_port_info->sm_sl = sm_sl;
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__parse_port_info_file:  "
		"Obtained Port:%d = state:%d, lid:0x%04X, lmc:%d, sm_lid:0x%04X, sm_sl:%d\n",
		port_num, sim_port_info->state, sim_port_info->lid,
		sim_port_info->lmc, sim_port_info->sm_lid,
		sim_port_info->sm_sl);

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return status;
}

/**********************************************************************
 * Parse the port guid_tbl file to obtain the port guid.
 * File format is:
 * [  0] fe80:0000:0000:0000:0002:c900:0120:3472
 **********************************************************************/
static ib_api_status_t
__get_port_guid_from_port_gid_tbl(IN osm_vendor_t * const p_vend,
				  IN uint32_t hca_idx,
				  IN uint8_t port_num, OUT uint64_t * port_guid)
{
	ib_api_status_t status = IB_ERROR;
	int info_file;
	char file_name[256];
	char file_buffer[3200];
	char *p_ch;
	int g[8];
	uint32_t len;

	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__get_port_guid_from_port_gid_tbl: "
		"Parsing Proc File System Port Guid Table CA %d Port %d.\n",
		hca_idx, port_num);

	/* we use the proc file system so we must be able to open the info file .. */
	sprintf(file_name, "%s/ca%d/port%d/gid_table",
		__get_simulator_dir(), hca_idx, port_num);
	info_file = open(file_name, O_RDONLY);
	if (!info_file) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__get_port_guid_from_port_gid_tbl: ERR 5123: "
			"Fail to open HCA:%d Port:%d gid_table file:(%s).\n",
			hca_idx, port_num, file_name);
		goto Exit;
	}

	/* read in the file */
	len = read(info_file, file_buffer, 3200);
	close(info_file);
	file_buffer[len] = '\0';

	/*
	   parse the file ...
	   [  0] fe80:0000:0000:0000:0002:c900:0120:3472
	   ...
	 */
	if (!(p_ch = strstr(file_buffer, "[  0]"))) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__get_port_guid_from_port_gid_tbl: ERR 5124: "
			"Fail to obtain first gid index. In gid_table file:(%s).\n",
			file_buffer);
		goto Exit;
	}
	if (sscanf(p_ch + 6, "%x:%x:%x:%x:%x:%x:%x:%x",
		   &g[7], &g[6], &g[5], &g[4], &g[3], &g[2], &g[1], &g[0]) != 8)
	{
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__get_port_guid_from_port_gid_tbl: ERR 5125: "
			"Fail to parse gid from gid_table file:(%s).\n", p_ch);
		goto Exit;
	}

	*port_guid =
	    (uint64_t) g[3] << 48 | (uint64_t) g[2] << 32 | (uint64_t) g[1] <<
	    16 | g[0];
	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return status;
}

/**********************************************************************
 * Initialize an Info Struct for the Given HCA by its index 1..N
 **********************************************************************/
static ib_api_status_t
__osm_ca_info_init(IN osm_vendor_t * const p_vend,
		   IN uint32_t const idx, OUT osm_ca_info_t * const p_ca_info)
{
	ib_api_status_t status = IB_ERROR;
	uint8_t port_num;
	uint64_t port_guid;

	sim_ca_info_t sim_ca_info;

	OSM_LOG_ENTER(p_vend->p_log);

	/* parse the CA info file */
	if (__parse_ca_info_file(p_vend, idx, &sim_ca_info) != IB_SUCCESS)
		goto Exit;

	p_ca_info->guid = cl_hton64(sim_ca_info.guid);

	/* set size of attributes and allocate them */
	p_ca_info->attr_size = 1;
	p_ca_info->p_attr = (ib_ca_attr_t *) malloc(sizeof(ib_ca_attr_t));

	p_ca_info->p_attr->ca_guid = p_ca_info->guid;
	p_ca_info->p_attr->num_ports = sim_ca_info.num_ports;

	/* now obtain the attributes of the ports */
	p_ca_info->p_attr->p_port_attr =
	    (ib_port_attr_t *) malloc(sim_ca_info.num_ports *
				      sizeof(ib_port_attr_t));

	/* get all the ports info */
	for (port_num = 1; port_num <= sim_ca_info.num_ports; port_num++) {
		sim_port_info_t sim_port_info;
		/* query the port attributes */
		if (__parse_port_info_file
		    (p_vend, idx, port_num, &sim_port_info)) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"__osm_ca_info_init: ERR 5126: "
				"Fail to get HCA:%d Port:%d Attributes.\n", idx,
				port_num);
			goto Exit;
		}

		/* HACK: the lids should have been converted to network but the rest of the code
		   is wrong and provdes them as is (host order) - so we stick with it. */
		p_ca_info->p_attr->p_port_attr[port_num - 1].lid =
		    sim_port_info.lid;
		p_ca_info->p_attr->p_port_attr[port_num - 1].link_state =
		    sim_port_info.state;
		p_ca_info->p_attr->p_port_attr[port_num - 1].sm_lid =
		    sim_port_info.sm_lid;

		/* get the port guid */
		if (__get_port_guid_from_port_gid_tbl
		    (p_vend, idx, port_num, &port_guid)) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"__osm_ca_info_init: ERR 5127: "
				"Fail to get HCA:%d Port:%d Guid.\n", idx,
				port_num);
			goto Exit;
		}
		p_ca_info->p_attr->p_port_attr[port_num - 1].port_guid =
		    cl_hton64(port_guid);
	}

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void
osm_ca_info_destroy(IN osm_vendor_t * const p_vend,
		    IN osm_ca_info_t * const p_ca_info, IN uint8_t num_ca)
{
	osm_ca_info_t *p_ca;
	uint8_t i;

	OSM_LOG_ENTER(p_vend->p_log);

	for (i = 0; i < num_ca; i++) {
		p_ca = &p_ca_info[i];

		if (NULL != p_ca->p_attr) {
			if (0 != p_ca->p_attr->num_ports) {
				free(p_ca->p_attr->p_port_attr);
			}

			free(p_ca->p_attr);
		}
	}

	free(p_ca_info);

	OSM_LOG_EXIT(p_vend->p_log);
}

/**********************************************************************
 * Fill in the array of port_attr with all available ports on ALL the
 * avilable CAs on this machine.
 **********************************************************************/
ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports)
{
	ib_api_status_t status = IB_SUCCESS;

	uint32_t caIdx;
	uint32_t ca_count = 0;
	uint32_t port_count = 0;
	uint8_t port_num;
	uint32_t total_ports = 0;
	osm_ca_info_t *p_ca_infos = NULL;
	uint32_t attr_array_sz = *p_num_ports;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);

	/* determine the number of CA's */
	ca_count = __hca_sim_get_num_cas();
	if (!ca_count) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 5128: "
			"Fail to get Any CA Ids.\n");
		goto Exit;
	}

	/* Allocate an array big enough to hold the ca info objects */
	p_ca_infos = malloc(ca_count * sizeof(osm_ca_info_t));
	if (p_ca_infos == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 5129: "
			"Unable to allocate CA information array.\n");
		goto Exit;
	}

	memset(p_ca_infos, 0, ca_count * sizeof(osm_ca_info_t));

	/*
	 * For each CA, retrieve the CA info attributes
	 */
	for (caIdx = 1; caIdx <= ca_count; caIdx++) {
		status =
		    __osm_ca_info_init(p_vend, caIdx, &p_ca_infos[caIdx - 1]);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_get_all_port_attr: ERR 5130: "
				"Unable to initialize CA Info object (%s).\n",
				ib_get_err_str(status));
			goto Exit;
		}
		total_ports += p_ca_infos[caIdx - 1].p_attr->num_ports;
	}

	*p_num_ports = total_ports;
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_get_all_port_attr: total ports:%u \n", total_ports);

	/*
	 * If the user supplied enough storage, return the port guids,
	 * otherwise, return the appropriate error.
	 */
	if (attr_array_sz >= total_ports) {
		for (caIdx = 1; caIdx <= ca_count; caIdx++) {
			uint32_t num_ports;

			num_ports = p_ca_infos[caIdx - 1].p_attr->num_ports;

			for (port_num = 0; port_num < num_ports; port_num++) {
				p_attr_array[port_count] =
				    *__osm_ca_info_get_port_attr_ptr(&p_ca_infos
								     [caIdx -
								      1],
								     port_num);
				port_count++;
			}
		}
	} else {
		status = IB_INSUFFICIENT_MEMORY;
		goto Exit;
	}

	status = IB_SUCCESS;

Exit:
	if (p_ca_infos) {
		osm_ca_info_destroy(p_vend, p_ca_infos, ca_count);
	}

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * Given the vendor obj and a port guid
 * return the ca id and port number that have that guid
 **********************************************************************/

ib_api_status_t
osm_vendor_get_guid_ca_and_port(IN osm_vendor_t * const p_vend,
				IN ib_net64_t const guid,
				OUT uint32_t * p_hca_hndl,
				OUT char *p_hca_id,
				OUT uint8_t * p_hca_idx,
				OUT uint32_t * p_port_num)
{
	uint32_t caIdx;
	uint32_t ca_count = 0;
	uint8_t port_num;
	ib_api_status_t status = IB_ERROR;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);

	/* determine the number of CA's */
	ca_count = __hca_sim_get_num_cas();
	if (!ca_count) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_guid_ca_and_port: ERR 5131: "
			"Fail to get Any CA Ids.\n");
		goto Exit;
	}

	/*
	 * For each CA, retrieve the CA info attributes
	 */
	for (caIdx = 1; caIdx <= ca_count; caIdx++) {
		sim_ca_info_t sim_ca_info;
		if (__parse_ca_info_file(p_vend, caIdx, &sim_ca_info) ==
		    IB_SUCCESS) {
			/* get all the ports info */
			for (port_num = 1; port_num <= sim_ca_info.num_ports;
			     port_num++) {
				uint64_t port_guid;
				if (!__get_port_guid_from_port_gid_tbl
				    (p_vend, caIdx, port_num, &port_guid)) {
					if (cl_hton64(port_guid) == guid) {
						osm_log(p_vend->p_log,
							OSM_LOG_DEBUG,
							"osm_vendor_get_guid_ca_and_port: "
							"Found Matching guid on HCA:%d Port:%d.\n",
							caIdx, port_num);
						strcpy(p_hca_id,
						       sim_ca_info.name);
						*p_port_num = port_num;
						*p_hca_idx = caIdx - 1;
						*p_hca_hndl = 0;
						status = IB_SUCCESS;
						goto Exit;
					}
				}
			}
		}
	}

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_get_guid_ca_and_port: ERR 5132: "
		"Fail to find HCA and Port for Port Guid 0x%" PRIx64 "\n",
		cl_ntoh64(guid));
	status = IB_INVALID_GUID;

Exit:

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/**********************************************************************
 * Given the vendor obj HCA ID and Port Num
 * update the given port guid if found. Return 0 on success.
 **********************************************************************/

ib_api_status_t
osm_vendor_get_guid_by_ca_and_port(IN osm_vendor_t * const p_vend,
				   IN char *hca_id,
				   IN uint32_t port_num,
				   OUT uint64_t * p_port_guid)
{
	uint32_t caIdx;
	uint32_t ca_count = 0;
	ib_api_status_t status = IB_ERROR;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);

	/* determine the number of CA's */
	ca_count = __hca_sim_get_num_cas();
	if (!ca_count) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_guid_by_ca_and_port: ERR 5133: "
			"Fail to get Any CA Ids.\n");
		goto Exit;
	}

	/*
	 * For each CA, retrieve the CA info attributes
	 */
	for (caIdx = 1; caIdx <= ca_count; caIdx++) {
		sim_ca_info_t sim_ca_info;
		if (__parse_ca_info_file(p_vend, caIdx, &sim_ca_info) ==
		    IB_SUCCESS) {
			/* if not identical by id - go to next one */
			if (strcmp(sim_ca_info.name, hca_id))
				continue;

			if ((port_num < 1)
			    || (port_num > sim_ca_info.num_ports)) {
				return 1;
			}

			if (!__get_port_guid_from_port_gid_tbl
			    (p_vend, caIdx, port_num, p_port_guid)) {
				osm_log(p_vend->p_log, OSM_LOG_DEBUG,
					"osm_vendor_get_guid_by_ca_and_port: "
					"Found Matching guid on HCA:%d Port:%d.\n",
					caIdx, port_num);
				status = IB_SUCCESS;
				goto Exit;
			}
		}
	}

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_get_guid_by_ca_and_port: ERR 5134: "
		"Fail to find HCA:%s\n", hca_id);
	status = IB_INVALID_GUID;

Exit:

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

#endif

/* Cypress West Bridge API source file (cyaslep2pep.c)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#include "../../include/linux/westbridge/cyashal.h"
#include "../../include/linux/westbridge/cyasusb.h"
#include "../../include/linux/westbridge/cyaserr.h"
#include "../../include/linux/westbridge/cyaslowlevel.h"
#include "../../include/linux/westbridge/cyasdma.h"

typedef enum cy_as_physical_endpoint_state {
	cy_as_e_p_free,
	cy_as_e_p_in,
	cy_as_e_p_out,
	cy_as_e_p_iso_in,
	cy_as_e_p_iso_out
} cy_as_physical_endpoint_state;


/*
* This map is used to map an index between 1 and 10
* to a logical endpoint number.  This is used to map
* LEP register indexes into actual EP numbers.
*/
static cy_as_end_point_number_t end_point_map[] = {
	3, 5, 7, 9, 10, 11, 12, 13, 14, 15 };

#define CY_AS_EPCFG_1024			(1 << 3)
#define CY_AS_EPCFG_DBL			 (0x02)
#define CY_AS_EPCFG_TRIPLE		  (0x03)
#define CY_AS_EPCFG_QUAD			(0x00)

/*
 * NB: This table contains the register values for PEP1
 * and PEP3.  PEP2 and PEP4 only have a bit to change the
 * direction of the PEP and therefre are not represented
 * in this table.
 */
static uint8_t pep_register_values[12][4] = {
	/* Bit 1:0 buffering, 0 = quad, 2 = double, 3 = triple */
	/* Bit 3 size, 0 = 512, 1 = 1024 */
	{
		CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_DBL,
	},/* Config 1  - PEP1 (2 * 512), PEP2 (2 * 512),
	   * PEP3 (2 * 512), PEP4 (2 * 512) */
	{
		CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_QUAD,
	}, /* Config 2  - PEP1 (2 * 512), PEP2 (2 * 512),
		* PEP3 (4 * 512), PEP4 (N/A) */
	{
		CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_DBL | CY_AS_EPCFG_1024,
	},/* Config 3  - PEP1 (2 * 512), PEP2 (2 * 512),
	   * PEP3 (2 * 1024), PEP4(N/A) */
	{
		CY_AS_EPCFG_QUAD,
		CY_AS_EPCFG_DBL,
	},/* Config 4  - PEP1 (4 * 512), PEP2 (N/A),
	   * PEP3 (2 * 512), PEP4 (2 * 512) */
	{
		CY_AS_EPCFG_QUAD,
		CY_AS_EPCFG_QUAD,
	},/* Config 5  - PEP1 (4 * 512), PEP2 (N/A),
	   * PEP3 (4 * 512), PEP4 (N/A) */
	{
		CY_AS_EPCFG_QUAD,
		CY_AS_EPCFG_1024 | CY_AS_EPCFG_DBL,
	},/* Config 6  - PEP1 (4 * 512), PEP2 (N/A),
	   * PEP3 (2 * 1024), PEP4 (N/A) */
	{
		CY_AS_EPCFG_1024 | CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_DBL,
	},/* Config 7  - PEP1 (2 * 1024), PEP2 (N/A),
	   * PEP3 (2 * 512), PEP4 (2 * 512) */
	{
		CY_AS_EPCFG_1024 | CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_QUAD,
	},/* Config 8  - PEP1 (2 * 1024), PEP2 (N/A),
	   * PEP3 (4 * 512), PEP4 (N/A) */
	{
		CY_AS_EPCFG_1024 | CY_AS_EPCFG_DBL,
		CY_AS_EPCFG_1024 | CY_AS_EPCFG_DBL,
	},/* Config 9  - PEP1 (2 * 1024), PEP2 (N/A),
	   * PEP3 (2 * 1024), PEP4 (N/A)*/
	{
		CY_AS_EPCFG_TRIPLE,
		CY_AS_EPCFG_TRIPLE,
	},/* Config 10 - PEP1 (3 * 512), PEP2 (N/A),
	   * PEP3 (3 * 512), PEP4 (2 * 512)*/
	{
		CY_AS_EPCFG_TRIPLE | CY_AS_EPCFG_1024,
		CY_AS_EPCFG_DBL,
	},/* Config 11 - PEP1 (3 * 1024), PEP2 (N/A),
	   * PEP3 (N/A), PEP4 (2 * 512) */
	{
		CY_AS_EPCFG_QUAD | CY_AS_EPCFG_1024,
		CY_AS_EPCFG_DBL,
	},/* Config 12 - PEP1 (4 * 1024), PEP2 (N/A),
	   * PEP3 (N/A), PEP4 (N/A) */
};

static cy_as_return_status_t
find_endpoint_directions(cy_as_device *dev_p,
	cy_as_physical_endpoint_state epstate[4])
{
	int i;
	cy_as_physical_endpoint_state desired;

	/*
	 * note, there is no error checking here because
	 * ISO error checking happens when the API is called.
	 */
	for (i = 0; i < 10; i++) {
		int epno = end_point_map[i];
		if (dev_p->usb_config[epno].enabled) {
			int pep = dev_p->usb_config[epno].physical;
			if (dev_p->usb_config[epno].type == cy_as_usb_iso) {
				/*
				 * marking this as an ISO endpoint, removes the
				 * physical EP from consideration when
				 * mapping the remaining E_ps.
				 */
				if (dev_p->usb_config[epno].dir == cy_as_usb_in)
					desired = cy_as_e_p_iso_in;
				else
					desired = cy_as_e_p_iso_out;
			} else {
				if (dev_p->usb_config[epno].dir == cy_as_usb_in)
					desired = cy_as_e_p_in;
				else
					desired = cy_as_e_p_out;
			}

			/*
			 * NB: Note the API calls insure that an ISO endpoint
			 * has a physical and logical EP number that are the
			 * same, therefore this condition is not enforced here.
			 */
			if (epstate[pep - 1] !=
				cy_as_e_p_free && epstate[pep - 1] != desired)
				return CY_AS_ERROR_INVALID_CONFIGURATION;

			epstate[pep - 1] = desired;
		}
	}

	/*
	 * create the EP1 config values directly.
	 * both EP1OUT and EP1IN are invalid by default.
	 */
	dev_p->usb_ep1cfg[0] = 0;
	dev_p->usb_ep1cfg[1] = 0;
	if (dev_p->usb_config[1].enabled) {
		if ((dev_p->usb_config[1].dir == cy_as_usb_out) ||
			(dev_p->usb_config[1].dir == cy_as_usb_in_out)) {
			/* Set the valid bit and type field. */
			dev_p->usb_ep1cfg[0] = (1 << 7);
			if (dev_p->usb_config[1].type == cy_as_usb_bulk)
				dev_p->usb_ep1cfg[0] |= (2 << 4);
			else
				dev_p->usb_ep1cfg[0] |= (3 << 4);
		}

		if ((dev_p->usb_config[1].dir == cy_as_usb_in) ||
		(dev_p->usb_config[1].dir == cy_as_usb_in_out)) {
			/* Set the valid bit and type field. */
			dev_p->usb_ep1cfg[1] = (1 << 7);
			if (dev_p->usb_config[1].type == cy_as_usb_bulk)
				dev_p->usb_ep1cfg[1] |= (2 << 4);
			else
				dev_p->usb_ep1cfg[1] |= (3 << 4);
		}
	}

	return CY_AS_ERROR_SUCCESS;
}

static void
create_register_settings(cy_as_device *dev_p,
	cy_as_physical_endpoint_state epstate[4])
{
	int i;
	uint8_t v;

	for (i = 0; i < 4; i++) {
		if (i == 0) {
			/* Start with the values that specify size */
			dev_p->usb_pepcfg[i] =
				pep_register_values
					[dev_p->usb_phy_config - 1][0];
		} else if (i == 2) {
			/* Start with the values that specify size */
			dev_p->usb_pepcfg[i] =
				pep_register_values
					[dev_p->usb_phy_config - 1][1];
		} else
			dev_p->usb_pepcfg[i] = 0;

		/* Adjust direction if it is in */
		if (epstate[i] == cy_as_e_p_iso_in ||
			epstate[i] == cy_as_e_p_in)
			dev_p->usb_pepcfg[i] |= (1 << 6);
	}

	/* Configure the logical EP registers */
	for (i = 0; i < 10; i++) {
		int val;
		int epnum = end_point_map[i];

		v = 0x10;	  /* PEP 1, Bulk Endpoint, EP not valid */
		if (dev_p->usb_config[epnum].enabled) {
			v |= (1 << 7);	 /* Enabled */

			val = dev_p->usb_config[epnum].physical - 1;
			cy_as_hal_assert(val >= 0 && val <= 3);
			v |= (val << 5);

			switch (dev_p->usb_config[epnum].type) {
			case cy_as_usb_bulk:
				val = 2;
				break;
			case cy_as_usb_int:
				val = 3;
				break;
			case cy_as_usb_iso:
				val = 1;
				break;
			default:
				cy_as_hal_assert(cy_false);
				break;
			}
			v |= (val << 3);
		}

		dev_p->usb_lepcfg[i] = v;
	}
}


cy_as_return_status_t
cy_as_usb_map_logical2_physical(cy_as_device *dev_p)
{
	cy_as_return_status_t ret;

	/* Physical EPs 3 5 7 9 respectively in the array */
	cy_as_physical_endpoint_state epstate[4] = {
		cy_as_e_p_free, cy_as_e_p_free,
			cy_as_e_p_free, cy_as_e_p_free };

	/* Find the direction for the endpoints */
	ret = find_endpoint_directions(dev_p, epstate);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/*
	 * now create the register settings based on the given
	 * assigned of logical E_ps to physical endpoints.
	 */
	create_register_settings(dev_p, epstate);

	return ret;
}

static uint16_t
get_max_dma_size(cy_as_device *dev_p, cy_as_end_point_number_t ep)
{
	uint16_t size = dev_p->usb_config[ep].size;

	if (size == 0) {
		switch (dev_p->usb_config[ep].type) {
		case cy_as_usb_control:
			size = 64;
			break;

		case cy_as_usb_bulk:
			size = cy_as_device_is_usb_high_speed(dev_p) ?
				512 : 64;
			break;

		case cy_as_usb_int:
			size = cy_as_device_is_usb_high_speed(dev_p) ?
				1024 : 64;
			break;

		case cy_as_usb_iso:
			size = cy_as_device_is_usb_high_speed(dev_p) ?
				1024 : 1023;
			break;
		}
	}

	return size;
}

cy_as_return_status_t
cy_as_usb_set_dma_sizes(cy_as_device *dev_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint32_t i;

	for (i = 0; i < 10; i++) {
		cy_as_usb_end_point_config *config_p =
			&dev_p->usb_config[end_point_map[i]];
		if (config_p->enabled) {
			ret = cy_as_dma_set_max_dma_size(dev_p,
				end_point_map[i],
				get_max_dma_size(dev_p, end_point_map[i]));
			if (ret != CY_AS_ERROR_SUCCESS)
				break;
		}
	}

	return ret;
}

cy_as_return_status_t
cy_as_usb_setup_dma(cy_as_device *dev_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint32_t i;

	for (i = 0; i < 10; i++) {
		cy_as_usb_end_point_config *config_p =
			&dev_p->usb_config[end_point_map[i]];
		if (config_p->enabled) {
			/* Map the endpoint direction to the DMA direction */
			cy_as_dma_direction dir = cy_as_direction_out;
			if (config_p->dir == cy_as_usb_in)
				dir = cy_as_direction_in;

			ret = cy_as_dma_enable_end_point(dev_p,
				end_point_map[i], cy_true, dir);
			if (ret != CY_AS_ERROR_SUCCESS)
				break;
		}
	}

	return ret;
}

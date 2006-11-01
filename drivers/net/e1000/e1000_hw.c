/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* e1000_hw.c
 * Shared functions for accessing and configuring the MAC
 */


#include "e1000_hw.h"

static int32_t e1000_swfw_sync_acquire(struct e1000_hw *hw, uint16_t mask);
static void e1000_swfw_sync_release(struct e1000_hw *hw, uint16_t mask);
static int32_t e1000_read_kmrn_reg(struct e1000_hw *hw, uint32_t reg_addr, uint16_t *data);
static int32_t e1000_write_kmrn_reg(struct e1000_hw *hw, uint32_t reg_addr, uint16_t data);
static int32_t e1000_get_software_semaphore(struct e1000_hw *hw);
static void e1000_release_software_semaphore(struct e1000_hw *hw);

static uint8_t e1000_arc_subsystem_valid(struct e1000_hw *hw);
static int32_t e1000_check_downshift(struct e1000_hw *hw);
static int32_t e1000_check_polarity(struct e1000_hw *hw, e1000_rev_polarity *polarity);
static void e1000_clear_hw_cntrs(struct e1000_hw *hw);
static void e1000_clear_vfta(struct e1000_hw *hw);
static int32_t e1000_commit_shadow_ram(struct e1000_hw *hw);
static int32_t e1000_config_dsp_after_link_change(struct e1000_hw *hw, boolean_t link_up);
static int32_t e1000_config_fc_after_link_up(struct e1000_hw *hw);
static int32_t e1000_detect_gig_phy(struct e1000_hw *hw);
static int32_t e1000_erase_ich8_4k_segment(struct e1000_hw *hw, uint32_t bank);
static int32_t e1000_get_auto_rd_done(struct e1000_hw *hw);
static int32_t e1000_get_cable_length(struct e1000_hw *hw, uint16_t *min_length, uint16_t *max_length);
static int32_t e1000_get_hw_eeprom_semaphore(struct e1000_hw *hw);
static int32_t e1000_get_phy_cfg_done(struct e1000_hw *hw);
static int32_t e1000_get_software_flag(struct e1000_hw *hw);
static int32_t e1000_ich8_cycle_init(struct e1000_hw *hw);
static int32_t e1000_ich8_flash_cycle(struct e1000_hw *hw, uint32_t timeout);
static int32_t e1000_id_led_init(struct e1000_hw *hw);
static int32_t e1000_init_lcd_from_nvm_config_region(struct e1000_hw *hw, uint32_t cnf_base_addr, uint32_t cnf_size);
static int32_t e1000_init_lcd_from_nvm(struct e1000_hw *hw);
static void e1000_init_rx_addrs(struct e1000_hw *hw);
static void e1000_initialize_hardware_bits(struct e1000_hw *hw);
static boolean_t e1000_is_onboard_nvm_eeprom(struct e1000_hw *hw);
static int32_t e1000_kumeran_lock_loss_workaround(struct e1000_hw *hw);
static int32_t e1000_mng_enable_host_if(struct e1000_hw *hw);
static int32_t e1000_mng_host_if_write(struct e1000_hw *hw, uint8_t *buffer, uint16_t length, uint16_t offset, uint8_t *sum);
static int32_t e1000_mng_write_cmd_header(struct e1000_hw* hw, struct e1000_host_mng_command_header* hdr);
static int32_t e1000_mng_write_commit(struct e1000_hw *hw);
static int32_t e1000_phy_ife_get_info(struct e1000_hw *hw, struct e1000_phy_info *phy_info);
static int32_t e1000_phy_igp_get_info(struct e1000_hw *hw, struct e1000_phy_info *phy_info);
static int32_t e1000_read_eeprom_eerd(struct e1000_hw *hw, uint16_t offset, uint16_t words, uint16_t *data);
static int32_t e1000_write_eeprom_eewr(struct e1000_hw *hw, uint16_t offset, uint16_t words, uint16_t *data);
static int32_t e1000_poll_eerd_eewr_done(struct e1000_hw *hw, int eerd);
static int32_t e1000_phy_m88_get_info(struct e1000_hw *hw, struct e1000_phy_info *phy_info);
static void e1000_put_hw_eeprom_semaphore(struct e1000_hw *hw);
static int32_t e1000_read_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t *data);
static int32_t e1000_verify_write_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t byte);
static int32_t e1000_write_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t byte);
static int32_t e1000_read_ich8_word(struct e1000_hw *hw, uint32_t index, uint16_t *data);
static int32_t e1000_read_ich8_data(struct e1000_hw *hw, uint32_t index, uint32_t size, uint16_t *data);
static int32_t e1000_write_ich8_data(struct e1000_hw *hw, uint32_t index, uint32_t size, uint16_t data);
static int32_t e1000_read_eeprom_ich8(struct e1000_hw *hw, uint16_t offset, uint16_t words, uint16_t *data);
static int32_t e1000_write_eeprom_ich8(struct e1000_hw *hw, uint16_t offset, uint16_t words, uint16_t *data);
static void e1000_release_software_flag(struct e1000_hw *hw);
static int32_t e1000_set_d3_lplu_state(struct e1000_hw *hw, boolean_t active);
static int32_t e1000_set_d0_lplu_state(struct e1000_hw *hw, boolean_t active);
static int32_t e1000_set_pci_ex_no_snoop(struct e1000_hw *hw, uint32_t no_snoop);
static void e1000_set_pci_express_master_disable(struct e1000_hw *hw);
static int32_t e1000_wait_autoneg(struct e1000_hw *hw);
static void e1000_write_reg_io(struct e1000_hw *hw, uint32_t offset, uint32_t value);
static int32_t e1000_set_phy_type(struct e1000_hw *hw);
static void e1000_phy_init_script(struct e1000_hw *hw);
static int32_t e1000_setup_copper_link(struct e1000_hw *hw);
static int32_t e1000_setup_fiber_serdes_link(struct e1000_hw *hw);
static int32_t e1000_adjust_serdes_amplitude(struct e1000_hw *hw);
static int32_t e1000_phy_force_speed_duplex(struct e1000_hw *hw);
static int32_t e1000_config_mac_to_phy(struct e1000_hw *hw);
static void e1000_raise_mdi_clk(struct e1000_hw *hw, uint32_t *ctrl);
static void e1000_lower_mdi_clk(struct e1000_hw *hw, uint32_t *ctrl);
static void e1000_shift_out_mdi_bits(struct e1000_hw *hw, uint32_t data,
                                     uint16_t count);
static uint16_t e1000_shift_in_mdi_bits(struct e1000_hw *hw);
static int32_t e1000_phy_reset_dsp(struct e1000_hw *hw);
static int32_t e1000_write_eeprom_spi(struct e1000_hw *hw, uint16_t offset,
                                      uint16_t words, uint16_t *data);
static int32_t e1000_write_eeprom_microwire(struct e1000_hw *hw,
                                            uint16_t offset, uint16_t words,
                                            uint16_t *data);
static int32_t e1000_spi_eeprom_ready(struct e1000_hw *hw);
static void e1000_raise_ee_clk(struct e1000_hw *hw, uint32_t *eecd);
static void e1000_lower_ee_clk(struct e1000_hw *hw, uint32_t *eecd);
static void e1000_shift_out_ee_bits(struct e1000_hw *hw, uint16_t data,
                                    uint16_t count);
static int32_t e1000_write_phy_reg_ex(struct e1000_hw *hw, uint32_t reg_addr,
                                      uint16_t phy_data);
static int32_t e1000_read_phy_reg_ex(struct e1000_hw *hw,uint32_t reg_addr,
                                     uint16_t *phy_data);
static uint16_t e1000_shift_in_ee_bits(struct e1000_hw *hw, uint16_t count);
static int32_t e1000_acquire_eeprom(struct e1000_hw *hw);
static void e1000_release_eeprom(struct e1000_hw *hw);
static void e1000_standby_eeprom(struct e1000_hw *hw);
static int32_t e1000_set_vco_speed(struct e1000_hw *hw);
static int32_t e1000_polarity_reversal_workaround(struct e1000_hw *hw);
static int32_t e1000_set_phy_mode(struct e1000_hw *hw);
static int32_t e1000_host_if_read_cookie(struct e1000_hw *hw, uint8_t *buffer);
static uint8_t e1000_calculate_mng_checksum(char *buffer, uint32_t length);
static int32_t e1000_configure_kmrn_for_10_100(struct e1000_hw *hw,
                                               uint16_t duplex);
static int32_t e1000_configure_kmrn_for_1000(struct e1000_hw *hw);

/* IGP cable length table */
static const
uint16_t e1000_igp_cable_length_table[IGP01E1000_AGC_LENGTH_TABLE_SIZE] =
    { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
      5, 10, 10, 10, 10, 10, 10, 10, 20, 20, 20, 20, 20, 25, 25, 25,
      25, 25, 25, 25, 30, 30, 30, 30, 40, 40, 40, 40, 40, 40, 40, 40,
      40, 50, 50, 50, 50, 50, 50, 50, 60, 60, 60, 60, 60, 60, 60, 60,
      60, 70, 70, 70, 70, 70, 70, 80, 80, 80, 80, 80, 80, 90, 90, 90,
      90, 90, 90, 90, 90, 90, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
      100, 100, 100, 100, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
      110, 110, 110, 110, 110, 110, 120, 120, 120, 120, 120, 120, 120, 120, 120, 120};

static const
uint16_t e1000_igp_2_cable_length_table[IGP02E1000_AGC_LENGTH_TABLE_SIZE] =
    { 0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11, 13, 16, 18, 21,
      0, 0, 0, 3, 6, 10, 13, 16, 19, 23, 26, 29, 32, 35, 38, 41,
      6, 10, 14, 18, 22, 26, 30, 33, 37, 41, 44, 48, 51, 54, 58, 61,
      21, 26, 31, 35, 40, 44, 49, 53, 57, 61, 65, 68, 72, 75, 79, 82,
      40, 45, 51, 56, 61, 66, 70, 75, 79, 83, 87, 91, 94, 98, 101, 104,
      60, 66, 72, 77, 82, 87, 92, 96, 100, 104, 108, 111, 114, 117, 119, 121,
      83, 89, 95, 100, 105, 109, 113, 116, 119, 122, 124,
      104, 109, 114, 118, 121, 124};

/******************************************************************************
 * Set the phy type member in the hw struct.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
e1000_set_phy_type(struct e1000_hw *hw)
{
    DEBUGFUNC("e1000_set_phy_type");

    if (hw->mac_type == e1000_undefined)
        return -E1000_ERR_PHY_TYPE;

    switch (hw->phy_id) {
    case M88E1000_E_PHY_ID:
    case M88E1000_I_PHY_ID:
    case M88E1011_I_PHY_ID:
    case M88E1111_I_PHY_ID:
        hw->phy_type = e1000_phy_m88;
        break;
    case IGP01E1000_I_PHY_ID:
        if (hw->mac_type == e1000_82541 ||
            hw->mac_type == e1000_82541_rev_2 ||
            hw->mac_type == e1000_82547 ||
            hw->mac_type == e1000_82547_rev_2) {
            hw->phy_type = e1000_phy_igp;
            break;
        }
    case IGP03E1000_E_PHY_ID:
        hw->phy_type = e1000_phy_igp_3;
        break;
    case IFE_E_PHY_ID:
    case IFE_PLUS_E_PHY_ID:
    case IFE_C_E_PHY_ID:
        hw->phy_type = e1000_phy_ife;
        break;
    case GG82563_E_PHY_ID:
        if (hw->mac_type == e1000_80003es2lan) {
            hw->phy_type = e1000_phy_gg82563;
            break;
        }
        /* Fall Through */
    default:
        /* Should never have loaded on this device */
        hw->phy_type = e1000_phy_undefined;
        return -E1000_ERR_PHY_TYPE;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * IGP phy init script - initializes the GbE PHY
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
e1000_phy_init_script(struct e1000_hw *hw)
{
    uint32_t ret_val;
    uint16_t phy_saved_data;

    DEBUGFUNC("e1000_phy_init_script");

    if (hw->phy_init_script) {
        msleep(20);

        /* Save off the current value of register 0x2F5B to be restored at
         * the end of this routine. */
        ret_val = e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

        /* Disabled the PHY transmitter */
        e1000_write_phy_reg(hw, 0x2F5B, 0x0003);

        msleep(20);

        e1000_write_phy_reg(hw,0x0000,0x0140);

        msleep(5);

        switch (hw->mac_type) {
        case e1000_82541:
        case e1000_82547:
            e1000_write_phy_reg(hw, 0x1F95, 0x0001);

            e1000_write_phy_reg(hw, 0x1F71, 0xBD21);

            e1000_write_phy_reg(hw, 0x1F79, 0x0018);

            e1000_write_phy_reg(hw, 0x1F30, 0x1600);

            e1000_write_phy_reg(hw, 0x1F31, 0x0014);

            e1000_write_phy_reg(hw, 0x1F32, 0x161C);

            e1000_write_phy_reg(hw, 0x1F94, 0x0003);

            e1000_write_phy_reg(hw, 0x1F96, 0x003F);

            e1000_write_phy_reg(hw, 0x2010, 0x0008);
            break;

        case e1000_82541_rev_2:
        case e1000_82547_rev_2:
            e1000_write_phy_reg(hw, 0x1F73, 0x0099);
            break;
        default:
            break;
        }

        e1000_write_phy_reg(hw, 0x0000, 0x3300);

        msleep(20);

        /* Now enable the transmitter */
        e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

        if (hw->mac_type == e1000_82547) {
            uint16_t fused, fine, coarse;

            /* Move to analog registers page */
            e1000_read_phy_reg(hw, IGP01E1000_ANALOG_SPARE_FUSE_STATUS, &fused);

            if (!(fused & IGP01E1000_ANALOG_SPARE_FUSE_ENABLED)) {
                e1000_read_phy_reg(hw, IGP01E1000_ANALOG_FUSE_STATUS, &fused);

                fine = fused & IGP01E1000_ANALOG_FUSE_FINE_MASK;
                coarse = fused & IGP01E1000_ANALOG_FUSE_COARSE_MASK;

                if (coarse > IGP01E1000_ANALOG_FUSE_COARSE_THRESH) {
                    coarse -= IGP01E1000_ANALOG_FUSE_COARSE_10;
                    fine -= IGP01E1000_ANALOG_FUSE_FINE_1;
                } else if (coarse == IGP01E1000_ANALOG_FUSE_COARSE_THRESH)
                    fine -= IGP01E1000_ANALOG_FUSE_FINE_10;

                fused = (fused & IGP01E1000_ANALOG_FUSE_POLY_MASK) |
                        (fine & IGP01E1000_ANALOG_FUSE_FINE_MASK) |
                        (coarse & IGP01E1000_ANALOG_FUSE_COARSE_MASK);

                e1000_write_phy_reg(hw, IGP01E1000_ANALOG_FUSE_CONTROL, fused);
                e1000_write_phy_reg(hw, IGP01E1000_ANALOG_FUSE_BYPASS,
                                    IGP01E1000_ANALOG_FUSE_ENABLE_SW_CONTROL);
            }
        }
    }
}

/******************************************************************************
 * Set the mac type member in the hw struct.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_set_mac_type(struct e1000_hw *hw)
{
    DEBUGFUNC("e1000_set_mac_type");

    switch (hw->device_id) {
    case E1000_DEV_ID_82542:
        switch (hw->revision_id) {
        case E1000_82542_2_0_REV_ID:
            hw->mac_type = e1000_82542_rev2_0;
            break;
        case E1000_82542_2_1_REV_ID:
            hw->mac_type = e1000_82542_rev2_1;
            break;
        default:
            /* Invalid 82542 revision ID */
            return -E1000_ERR_MAC_TYPE;
        }
        break;
    case E1000_DEV_ID_82543GC_FIBER:
    case E1000_DEV_ID_82543GC_COPPER:
        hw->mac_type = e1000_82543;
        break;
    case E1000_DEV_ID_82544EI_COPPER:
    case E1000_DEV_ID_82544EI_FIBER:
    case E1000_DEV_ID_82544GC_COPPER:
    case E1000_DEV_ID_82544GC_LOM:
        hw->mac_type = e1000_82544;
        break;
    case E1000_DEV_ID_82540EM:
    case E1000_DEV_ID_82540EM_LOM:
    case E1000_DEV_ID_82540EP:
    case E1000_DEV_ID_82540EP_LOM:
    case E1000_DEV_ID_82540EP_LP:
        hw->mac_type = e1000_82540;
        break;
    case E1000_DEV_ID_82545EM_COPPER:
    case E1000_DEV_ID_82545EM_FIBER:
        hw->mac_type = e1000_82545;
        break;
    case E1000_DEV_ID_82545GM_COPPER:
    case E1000_DEV_ID_82545GM_FIBER:
    case E1000_DEV_ID_82545GM_SERDES:
        hw->mac_type = e1000_82545_rev_3;
        break;
    case E1000_DEV_ID_82546EB_COPPER:
    case E1000_DEV_ID_82546EB_FIBER:
    case E1000_DEV_ID_82546EB_QUAD_COPPER:
        hw->mac_type = e1000_82546;
        break;
    case E1000_DEV_ID_82546GB_COPPER:
    case E1000_DEV_ID_82546GB_FIBER:
    case E1000_DEV_ID_82546GB_SERDES:
    case E1000_DEV_ID_82546GB_PCIE:
    case E1000_DEV_ID_82546GB_QUAD_COPPER:
    case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
        hw->mac_type = e1000_82546_rev_3;
        break;
    case E1000_DEV_ID_82541EI:
    case E1000_DEV_ID_82541EI_MOBILE:
    case E1000_DEV_ID_82541ER_LOM:
        hw->mac_type = e1000_82541;
        break;
    case E1000_DEV_ID_82541ER:
    case E1000_DEV_ID_82541GI:
    case E1000_DEV_ID_82541GI_LF:
    case E1000_DEV_ID_82541GI_MOBILE:
        hw->mac_type = e1000_82541_rev_2;
        break;
    case E1000_DEV_ID_82547EI:
    case E1000_DEV_ID_82547EI_MOBILE:
        hw->mac_type = e1000_82547;
        break;
    case E1000_DEV_ID_82547GI:
        hw->mac_type = e1000_82547_rev_2;
        break;
    case E1000_DEV_ID_82571EB_COPPER:
    case E1000_DEV_ID_82571EB_FIBER:
    case E1000_DEV_ID_82571EB_SERDES:
    case E1000_DEV_ID_82571EB_QUAD_COPPER:
    case E1000_DEV_ID_82571EB_QUAD_COPPER_LOWPROFILE:
            hw->mac_type = e1000_82571;
        break;
    case E1000_DEV_ID_82572EI_COPPER:
    case E1000_DEV_ID_82572EI_FIBER:
    case E1000_DEV_ID_82572EI_SERDES:
    case E1000_DEV_ID_82572EI:
        hw->mac_type = e1000_82572;
        break;
    case E1000_DEV_ID_82573E:
    case E1000_DEV_ID_82573E_IAMT:
    case E1000_DEV_ID_82573L:
        hw->mac_type = e1000_82573;
        break;
    case E1000_DEV_ID_80003ES2LAN_COPPER_SPT:
    case E1000_DEV_ID_80003ES2LAN_SERDES_SPT:
    case E1000_DEV_ID_80003ES2LAN_COPPER_DPT:
    case E1000_DEV_ID_80003ES2LAN_SERDES_DPT:
        hw->mac_type = e1000_80003es2lan;
        break;
    case E1000_DEV_ID_ICH8_IGP_M_AMT:
    case E1000_DEV_ID_ICH8_IGP_AMT:
    case E1000_DEV_ID_ICH8_IGP_C:
    case E1000_DEV_ID_ICH8_IFE:
    case E1000_DEV_ID_ICH8_IFE_GT:
    case E1000_DEV_ID_ICH8_IFE_G:
    case E1000_DEV_ID_ICH8_IGP_M:
        hw->mac_type = e1000_ich8lan;
        break;
    default:
        /* Should never have loaded on this device */
        return -E1000_ERR_MAC_TYPE;
    }

    switch (hw->mac_type) {
    case e1000_ich8lan:
        hw->swfwhw_semaphore_present = TRUE;
        hw->asf_firmware_present = TRUE;
        break;
    case e1000_80003es2lan:
        hw->swfw_sync_present = TRUE;
        /* fall through */
    case e1000_82571:
    case e1000_82572:
    case e1000_82573:
        hw->eeprom_semaphore_present = TRUE;
        /* fall through */
    case e1000_82541:
    case e1000_82547:
    case e1000_82541_rev_2:
    case e1000_82547_rev_2:
        hw->asf_firmware_present = TRUE;
        break;
    default:
        break;
    }

    return E1000_SUCCESS;
}

/*****************************************************************************
 * Set media type and TBI compatibility.
 *
 * hw - Struct containing variables accessed by shared code
 * **************************************************************************/
void
e1000_set_media_type(struct e1000_hw *hw)
{
    uint32_t status;

    DEBUGFUNC("e1000_set_media_type");

    if (hw->mac_type != e1000_82543) {
        /* tbi_compatibility is only valid on 82543 */
        hw->tbi_compatibility_en = FALSE;
    }

    switch (hw->device_id) {
    case E1000_DEV_ID_82545GM_SERDES:
    case E1000_DEV_ID_82546GB_SERDES:
    case E1000_DEV_ID_82571EB_SERDES:
    case E1000_DEV_ID_82572EI_SERDES:
    case E1000_DEV_ID_80003ES2LAN_SERDES_DPT:
        hw->media_type = e1000_media_type_internal_serdes;
        break;
    default:
        switch (hw->mac_type) {
        case e1000_82542_rev2_0:
        case e1000_82542_rev2_1:
            hw->media_type = e1000_media_type_fiber;
            break;
        case e1000_ich8lan:
        case e1000_82573:
            /* The STATUS_TBIMODE bit is reserved or reused for the this
             * device.
             */
            hw->media_type = e1000_media_type_copper;
            break;
        default:
            status = E1000_READ_REG(hw, STATUS);
            if (status & E1000_STATUS_TBIMODE) {
                hw->media_type = e1000_media_type_fiber;
                /* tbi_compatibility not valid on fiber */
                hw->tbi_compatibility_en = FALSE;
            } else {
                hw->media_type = e1000_media_type_copper;
            }
            break;
        }
    }
}

/******************************************************************************
 * Reset the transmit and receive units; mask and clear all interrupts.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_reset_hw(struct e1000_hw *hw)
{
    uint32_t ctrl;
    uint32_t ctrl_ext;
    uint32_t icr;
    uint32_t manc;
    uint32_t led_ctrl;
    uint32_t timeout;
    uint32_t extcnf_ctrl;
    int32_t ret_val;

    DEBUGFUNC("e1000_reset_hw");

    /* For 82542 (rev 2.0), disable MWI before issuing a device reset */
    if (hw->mac_type == e1000_82542_rev2_0) {
        DEBUGOUT("Disabling MWI on 82542 rev 2.0\n");
        e1000_pci_clear_mwi(hw);
    }

    if (hw->bus_type == e1000_bus_type_pci_express) {
        /* Prevent the PCI-E bus from sticking if there is no TLP connection
         * on the last TLP read/write transaction when MAC is reset.
         */
        if (e1000_disable_pciex_master(hw) != E1000_SUCCESS) {
            DEBUGOUT("PCI-E Master disable polling has failed.\n");
        }
    }

    /* Clear interrupt mask to stop board from generating interrupts */
    DEBUGOUT("Masking off all interrupts\n");
    E1000_WRITE_REG(hw, IMC, 0xffffffff);

    /* Disable the Transmit and Receive units.  Then delay to allow
     * any pending transactions to complete before we hit the MAC with
     * the global reset.
     */
    E1000_WRITE_REG(hw, RCTL, 0);
    E1000_WRITE_REG(hw, TCTL, E1000_TCTL_PSP);
    E1000_WRITE_FLUSH(hw);

    /* The tbi_compatibility_on Flag must be cleared when Rctl is cleared. */
    hw->tbi_compatibility_on = FALSE;

    /* Delay to allow any outstanding PCI transactions to complete before
     * resetting the device
     */
    msleep(10);

    ctrl = E1000_READ_REG(hw, CTRL);

    /* Must reset the PHY before resetting the MAC */
    if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
        E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_PHY_RST));
        msleep(5);
    }

    /* Must acquire the MDIO ownership before MAC reset.
     * Ownership defaults to firmware after a reset. */
    if (hw->mac_type == e1000_82573) {
        timeout = 10;

        extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
        extcnf_ctrl |= E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;

        do {
            E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
            extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);

            if (extcnf_ctrl & E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP)
                break;
            else
                extcnf_ctrl |= E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;

            msleep(2);
            timeout--;
        } while (timeout);
    }

    /* Workaround for ICH8 bit corruption issue in FIFO memory */
    if (hw->mac_type == e1000_ich8lan) {
        /* Set Tx and Rx buffer allocation to 8k apiece. */
        E1000_WRITE_REG(hw, PBA, E1000_PBA_8K);
        /* Set Packet Buffer Size to 16k. */
        E1000_WRITE_REG(hw, PBS, E1000_PBS_16K);
    }

    /* Issue a global reset to the MAC.  This will reset the chip's
     * transmit, receive, DMA, and link units.  It will not effect
     * the current PCI configuration.  The global reset bit is self-
     * clearing, and should clear within a microsecond.
     */
    DEBUGOUT("Issuing a global reset to MAC\n");

    switch (hw->mac_type) {
        case e1000_82544:
        case e1000_82540:
        case e1000_82545:
        case e1000_82546:
        case e1000_82541:
        case e1000_82541_rev_2:
            /* These controllers can't ack the 64-bit write when issuing the
             * reset, so use IO-mapping as a workaround to issue the reset */
            E1000_WRITE_REG_IO(hw, CTRL, (ctrl | E1000_CTRL_RST));
            break;
        case e1000_82545_rev_3:
        case e1000_82546_rev_3:
            /* Reset is performed on a shadow of the control register */
            E1000_WRITE_REG(hw, CTRL_DUP, (ctrl | E1000_CTRL_RST));
            break;
        case e1000_ich8lan:
            if (!hw->phy_reset_disable &&
                e1000_check_phy_reset_block(hw) == E1000_SUCCESS) {
                /* e1000_ich8lan PHY HW reset requires MAC CORE reset
                 * at the same time to make sure the interface between
                 * MAC and the external PHY is reset.
                 */
                ctrl |= E1000_CTRL_PHY_RST;
            }

            e1000_get_software_flag(hw);
            E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_RST));
            msleep(5);
            break;
        default:
            E1000_WRITE_REG(hw, CTRL, (ctrl | E1000_CTRL_RST));
            break;
    }

    /* After MAC reset, force reload of EEPROM to restore power-on settings to
     * device.  Later controllers reload the EEPROM automatically, so just wait
     * for reload to complete.
     */
    switch (hw->mac_type) {
        case e1000_82542_rev2_0:
        case e1000_82542_rev2_1:
        case e1000_82543:
        case e1000_82544:
            /* Wait for reset to complete */
            udelay(10);
            ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
            ctrl_ext |= E1000_CTRL_EXT_EE_RST;
            E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
            E1000_WRITE_FLUSH(hw);
            /* Wait for EEPROM reload */
            msleep(2);
            break;
        case e1000_82541:
        case e1000_82541_rev_2:
        case e1000_82547:
        case e1000_82547_rev_2:
            /* Wait for EEPROM reload */
            msleep(20);
            break;
        case e1000_82573:
            if (e1000_is_onboard_nvm_eeprom(hw) == FALSE) {
                udelay(10);
                ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
                ctrl_ext |= E1000_CTRL_EXT_EE_RST;
                E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
                E1000_WRITE_FLUSH(hw);
            }
            /* fall through */
        default:
            /* Auto read done will delay 5ms or poll based on mac type */
            ret_val = e1000_get_auto_rd_done(hw);
            if (ret_val)
                return ret_val;
            break;
    }

    /* Disable HW ARPs on ASF enabled adapters */
    if (hw->mac_type >= e1000_82540 && hw->mac_type <= e1000_82547_rev_2) {
        manc = E1000_READ_REG(hw, MANC);
        manc &= ~(E1000_MANC_ARP_EN);
        E1000_WRITE_REG(hw, MANC, manc);
    }

    if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
        e1000_phy_init_script(hw);

        /* Configure activity LED after PHY reset */
        led_ctrl = E1000_READ_REG(hw, LEDCTL);
        led_ctrl &= IGP_ACTIVITY_LED_MASK;
        led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
        E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
    }

    /* Clear interrupt mask to stop board from generating interrupts */
    DEBUGOUT("Masking off all interrupts\n");
    E1000_WRITE_REG(hw, IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    icr = E1000_READ_REG(hw, ICR);

    /* If MWI was previously enabled, reenable it. */
    if (hw->mac_type == e1000_82542_rev2_0) {
        if (hw->pci_cmd_word & PCI_COMMAND_INVALIDATE)
            e1000_pci_set_mwi(hw);
    }

    if (hw->mac_type == e1000_ich8lan) {
        uint32_t kab = E1000_READ_REG(hw, KABGTXD);
        kab |= E1000_KABGTXD_BGSQLBIAS;
        E1000_WRITE_REG(hw, KABGTXD, kab);
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 *
 * Initialize a number of hardware-dependent bits
 *
 * hw: Struct containing variables accessed by shared code
 *
 * This function contains hardware limitation workarounds for PCI-E adapters
 *
 *****************************************************************************/
static void
e1000_initialize_hardware_bits(struct e1000_hw *hw)
{
    if ((hw->mac_type >= e1000_82571) && (!hw->initialize_hw_bits_disable)) {
        /* Settings common to all PCI-express silicon */
        uint32_t reg_ctrl, reg_ctrl_ext;
        uint32_t reg_tarc0, reg_tarc1;
        uint32_t reg_tctl;
        uint32_t reg_txdctl, reg_txdctl1;

        /* link autonegotiation/sync workarounds */
        reg_tarc0 = E1000_READ_REG(hw, TARC0);
        reg_tarc0 &= ~((1 << 30)|(1 << 29)|(1 << 28)|(1 << 27));

        /* Enable not-done TX descriptor counting */
        reg_txdctl = E1000_READ_REG(hw, TXDCTL);
        reg_txdctl |= E1000_TXDCTL_COUNT_DESC;
        E1000_WRITE_REG(hw, TXDCTL, reg_txdctl);
        reg_txdctl1 = E1000_READ_REG(hw, TXDCTL1);
        reg_txdctl1 |= E1000_TXDCTL_COUNT_DESC;
        E1000_WRITE_REG(hw, TXDCTL1, reg_txdctl1);

        switch (hw->mac_type) {
            case e1000_82571:
            case e1000_82572:
                /* Clear PHY TX compatible mode bits */
                reg_tarc1 = E1000_READ_REG(hw, TARC1);
                reg_tarc1 &= ~((1 << 30)|(1 << 29));

                /* link autonegotiation/sync workarounds */
                reg_tarc0 |= ((1 << 26)|(1 << 25)|(1 << 24)|(1 << 23));

                /* TX ring control fixes */
                reg_tarc1 |= ((1 << 26)|(1 << 25)|(1 << 24));

                /* Multiple read bit is reversed polarity */
                reg_tctl = E1000_READ_REG(hw, TCTL);
                if (reg_tctl & E1000_TCTL_MULR)
                    reg_tarc1 &= ~(1 << 28);
                else
                    reg_tarc1 |= (1 << 28);

                E1000_WRITE_REG(hw, TARC1, reg_tarc1);
                break;
            case e1000_82573:
                reg_ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
                reg_ctrl_ext &= ~(1 << 23);
                reg_ctrl_ext |= (1 << 22);

                /* TX byte count fix */
                reg_ctrl = E1000_READ_REG(hw, CTRL);
                reg_ctrl &= ~(1 << 29);

                E1000_WRITE_REG(hw, CTRL_EXT, reg_ctrl_ext);
                E1000_WRITE_REG(hw, CTRL, reg_ctrl);
                break;
            case e1000_80003es2lan:
                /* improve small packet performace for fiber/serdes */
                if ((hw->media_type == e1000_media_type_fiber) ||
                    (hw->media_type == e1000_media_type_internal_serdes)) {
                    reg_tarc0 &= ~(1 << 20);
                }

                /* Multiple read bit is reversed polarity */
                reg_tctl = E1000_READ_REG(hw, TCTL);
                reg_tarc1 = E1000_READ_REG(hw, TARC1);
                if (reg_tctl & E1000_TCTL_MULR)
                    reg_tarc1 &= ~(1 << 28);
                else
                    reg_tarc1 |= (1 << 28);

                E1000_WRITE_REG(hw, TARC1, reg_tarc1);
                break;
            case e1000_ich8lan:
                /* Reduce concurrent DMA requests to 3 from 4 */
                if ((hw->revision_id < 3) ||
                    ((hw->device_id != E1000_DEV_ID_ICH8_IGP_M_AMT) &&
                     (hw->device_id != E1000_DEV_ID_ICH8_IGP_M)))
                    reg_tarc0 |= ((1 << 29)|(1 << 28));

                reg_ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
                reg_ctrl_ext |= (1 << 22);
                E1000_WRITE_REG(hw, CTRL_EXT, reg_ctrl_ext);

                /* workaround TX hang with TSO=on */
                reg_tarc0 |= ((1 << 27)|(1 << 26)|(1 << 24)|(1 << 23));

                /* Multiple read bit is reversed polarity */
                reg_tctl = E1000_READ_REG(hw, TCTL);
                reg_tarc1 = E1000_READ_REG(hw, TARC1);
                if (reg_tctl & E1000_TCTL_MULR)
                    reg_tarc1 &= ~(1 << 28);
                else
                    reg_tarc1 |= (1 << 28);

                /* workaround TX hang with TSO=on */
                reg_tarc1 |= ((1 << 30)|(1 << 26)|(1 << 24));

                E1000_WRITE_REG(hw, TARC1, reg_tarc1);
                break;
            default:
                break;
        }

        E1000_WRITE_REG(hw, TARC0, reg_tarc0);
    }
}

/******************************************************************************
 * Performs basic configuration of the adapter.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Assumes that the controller has previously been reset and is in a
 * post-reset uninitialized state. Initializes the receive address registers,
 * multicast table, and VLAN filter table. Calls routines to setup link
 * configuration and flow control settings. Clears all on-chip counters. Leaves
 * the transmit and receive units disabled and uninitialized.
 *****************************************************************************/
int32_t
e1000_init_hw(struct e1000_hw *hw)
{
    uint32_t ctrl;
    uint32_t i;
    int32_t ret_val;
    uint16_t pcix_cmd_word;
    uint16_t pcix_stat_hi_word;
    uint16_t cmd_mmrbc;
    uint16_t stat_mmrbc;
    uint32_t mta_size;
    uint32_t reg_data;
    uint32_t ctrl_ext;

    DEBUGFUNC("e1000_init_hw");

    /* force full DMA clock frequency for 10/100 on ICH8 A0-B0 */
    if ((hw->mac_type == e1000_ich8lan) &&
        ((hw->revision_id < 3) ||
         ((hw->device_id != E1000_DEV_ID_ICH8_IGP_M_AMT) &&
          (hw->device_id != E1000_DEV_ID_ICH8_IGP_M)))) {
            reg_data = E1000_READ_REG(hw, STATUS);
            reg_data &= ~0x80000000;
            E1000_WRITE_REG(hw, STATUS, reg_data);
    }

    /* Initialize Identification LED */
    ret_val = e1000_id_led_init(hw);
    if (ret_val) {
        DEBUGOUT("Error Initializing Identification LED\n");
        return ret_val;
    }

    /* Set the media type and TBI compatibility */
    e1000_set_media_type(hw);

    /* Must be called after e1000_set_media_type because media_type is used */
    e1000_initialize_hardware_bits(hw);

    /* Disabling VLAN filtering. */
    DEBUGOUT("Initializing the IEEE VLAN\n");
    /* VET hardcoded to standard value and VFTA removed in ICH8 LAN */
    if (hw->mac_type != e1000_ich8lan) {
        if (hw->mac_type < e1000_82545_rev_3)
            E1000_WRITE_REG(hw, VET, 0);
        e1000_clear_vfta(hw);
    }

    /* For 82542 (rev 2.0), disable MWI and put the receiver into reset */
    if (hw->mac_type == e1000_82542_rev2_0) {
        DEBUGOUT("Disabling MWI on 82542 rev 2.0\n");
        e1000_pci_clear_mwi(hw);
        E1000_WRITE_REG(hw, RCTL, E1000_RCTL_RST);
        E1000_WRITE_FLUSH(hw);
        msleep(5);
    }

    /* Setup the receive address. This involves initializing all of the Receive
     * Address Registers (RARs 0 - 15).
     */
    e1000_init_rx_addrs(hw);

    /* For 82542 (rev 2.0), take the receiver out of reset and enable MWI */
    if (hw->mac_type == e1000_82542_rev2_0) {
        E1000_WRITE_REG(hw, RCTL, 0);
        E1000_WRITE_FLUSH(hw);
        msleep(1);
        if (hw->pci_cmd_word & PCI_COMMAND_INVALIDATE)
            e1000_pci_set_mwi(hw);
    }

    /* Zero out the Multicast HASH table */
    DEBUGOUT("Zeroing the MTA\n");
    mta_size = E1000_MC_TBL_SIZE;
    if (hw->mac_type == e1000_ich8lan)
        mta_size = E1000_MC_TBL_SIZE_ICH8LAN;
    for (i = 0; i < mta_size; i++) {
        E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
        /* use write flush to prevent Memory Write Block (MWB) from
         * occuring when accessing our register space */
        E1000_WRITE_FLUSH(hw);
    }

    /* Set the PCI priority bit correctly in the CTRL register.  This
     * determines if the adapter gives priority to receives, or if it
     * gives equal priority to transmits and receives.  Valid only on
     * 82542 and 82543 silicon.
     */
    if (hw->dma_fairness && hw->mac_type <= e1000_82543) {
        ctrl = E1000_READ_REG(hw, CTRL);
        E1000_WRITE_REG(hw, CTRL, ctrl | E1000_CTRL_PRIOR);
    }

    switch (hw->mac_type) {
    case e1000_82545_rev_3:
    case e1000_82546_rev_3:
        break;
    default:
        /* Workaround for PCI-X problem when BIOS sets MMRBC incorrectly. */
        if (hw->bus_type == e1000_bus_type_pcix) {
            e1000_read_pci_cfg(hw, PCIX_COMMAND_REGISTER, &pcix_cmd_word);
            e1000_read_pci_cfg(hw, PCIX_STATUS_REGISTER_HI,
                &pcix_stat_hi_word);
            cmd_mmrbc = (pcix_cmd_word & PCIX_COMMAND_MMRBC_MASK) >>
                PCIX_COMMAND_MMRBC_SHIFT;
            stat_mmrbc = (pcix_stat_hi_word & PCIX_STATUS_HI_MMRBC_MASK) >>
                PCIX_STATUS_HI_MMRBC_SHIFT;
            if (stat_mmrbc == PCIX_STATUS_HI_MMRBC_4K)
                stat_mmrbc = PCIX_STATUS_HI_MMRBC_2K;
            if (cmd_mmrbc > stat_mmrbc) {
                pcix_cmd_word &= ~PCIX_COMMAND_MMRBC_MASK;
                pcix_cmd_word |= stat_mmrbc << PCIX_COMMAND_MMRBC_SHIFT;
                e1000_write_pci_cfg(hw, PCIX_COMMAND_REGISTER,
                    &pcix_cmd_word);
            }
        }
        break;
    }

    /* More time needed for PHY to initialize */
    if (hw->mac_type == e1000_ich8lan)
        msleep(15);

    /* Call a subroutine to configure the link and setup flow control. */
    ret_val = e1000_setup_link(hw);

    /* Set the transmit descriptor write-back policy */
    if (hw->mac_type > e1000_82544) {
        ctrl = E1000_READ_REG(hw, TXDCTL);
        ctrl = (ctrl & ~E1000_TXDCTL_WTHRESH) | E1000_TXDCTL_FULL_TX_DESC_WB;
        E1000_WRITE_REG(hw, TXDCTL, ctrl);
    }

    if (hw->mac_type == e1000_82573) {
        e1000_enable_tx_pkt_filtering(hw);
    }

    switch (hw->mac_type) {
    default:
        break;
    case e1000_80003es2lan:
        /* Enable retransmit on late collisions */
        reg_data = E1000_READ_REG(hw, TCTL);
        reg_data |= E1000_TCTL_RTLC;
        E1000_WRITE_REG(hw, TCTL, reg_data);

        /* Configure Gigabit Carry Extend Padding */
        reg_data = E1000_READ_REG(hw, TCTL_EXT);
        reg_data &= ~E1000_TCTL_EXT_GCEX_MASK;
        reg_data |= DEFAULT_80003ES2LAN_TCTL_EXT_GCEX;
        E1000_WRITE_REG(hw, TCTL_EXT, reg_data);

        /* Configure Transmit Inter-Packet Gap */
        reg_data = E1000_READ_REG(hw, TIPG);
        reg_data &= ~E1000_TIPG_IPGT_MASK;
        reg_data |= DEFAULT_80003ES2LAN_TIPG_IPGT_1000;
        E1000_WRITE_REG(hw, TIPG, reg_data);

        reg_data = E1000_READ_REG_ARRAY(hw, FFLT, 0x0001);
        reg_data &= ~0x00100000;
        E1000_WRITE_REG_ARRAY(hw, FFLT, 0x0001, reg_data);
        /* Fall through */
    case e1000_82571:
    case e1000_82572:
    case e1000_ich8lan:
        ctrl = E1000_READ_REG(hw, TXDCTL1);
        ctrl = (ctrl & ~E1000_TXDCTL_WTHRESH) | E1000_TXDCTL_FULL_TX_DESC_WB;
        E1000_WRITE_REG(hw, TXDCTL1, ctrl);
        break;
    }


    if (hw->mac_type == e1000_82573) {
        uint32_t gcr = E1000_READ_REG(hw, GCR);
        gcr |= E1000_GCR_L1_ACT_WITHOUT_L0S_RX;
        E1000_WRITE_REG(hw, GCR, gcr);
    }

    /* Clear all of the statistics registers (clear on read).  It is
     * important that we do this after we have tried to establish link
     * because the symbol error count will increment wildly if there
     * is no link.
     */
    e1000_clear_hw_cntrs(hw);

    /* ICH8 No-snoop bits are opposite polarity.
     * Set to snoop by default after reset. */
    if (hw->mac_type == e1000_ich8lan)
        e1000_set_pci_ex_no_snoop(hw, PCI_EX_82566_SNOOP_ALL);

    if (hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER ||
        hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3) {
        ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
        /* Relaxed ordering must be disabled to avoid a parity
         * error crash in a PCI slot. */
        ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
    }

    return ret_val;
}

/******************************************************************************
 * Adjust SERDES output amplitude based on EEPROM setting.
 *
 * hw - Struct containing variables accessed by shared code.
 *****************************************************************************/
static int32_t
e1000_adjust_serdes_amplitude(struct e1000_hw *hw)
{
    uint16_t eeprom_data;
    int32_t  ret_val;

    DEBUGFUNC("e1000_adjust_serdes_amplitude");

    if (hw->media_type != e1000_media_type_internal_serdes)
        return E1000_SUCCESS;

    switch (hw->mac_type) {
    case e1000_82545_rev_3:
    case e1000_82546_rev_3:
        break;
    default:
        return E1000_SUCCESS;
    }

    ret_val = e1000_read_eeprom(hw, EEPROM_SERDES_AMPLITUDE, 1, &eeprom_data);
    if (ret_val) {
        return ret_val;
    }

    if (eeprom_data != EEPROM_RESERVED_WORD) {
        /* Adjust SERDES output amplitude only. */
        eeprom_data &= EEPROM_SERDES_AMPLITUDE_MASK;
        ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_EXT_CTRL, eeprom_data);
        if (ret_val)
            return ret_val;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Configures flow control and link settings.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Determines which flow control settings to use. Calls the apropriate media-
 * specific link configuration function. Configures the flow control settings.
 * Assuming the adapter has a valid link partner, a valid link should be
 * established. Assumes the hardware has previously been reset and the
 * transmitter and receiver are not enabled.
 *****************************************************************************/
int32_t
e1000_setup_link(struct e1000_hw *hw)
{
    uint32_t ctrl_ext;
    int32_t ret_val;
    uint16_t eeprom_data;

    DEBUGFUNC("e1000_setup_link");

    /* In the case of the phy reset being blocked, we already have a link.
     * We do not have to set it up again. */
    if (e1000_check_phy_reset_block(hw))
        return E1000_SUCCESS;

    /* Read and store word 0x0F of the EEPROM. This word contains bits
     * that determine the hardware's default PAUSE (flow control) mode,
     * a bit that determines whether the HW defaults to enabling or
     * disabling auto-negotiation, and the direction of the
     * SW defined pins. If there is no SW over-ride of the flow
     * control setting, then the variable hw->fc will
     * be initialized based on a value in the EEPROM.
     */
    if (hw->fc == E1000_FC_DEFAULT) {
        switch (hw->mac_type) {
        case e1000_ich8lan:
        case e1000_82573:
            hw->fc = E1000_FC_FULL;
            break;
        default:
            ret_val = e1000_read_eeprom(hw, EEPROM_INIT_CONTROL2_REG,
                                        1, &eeprom_data);
            if (ret_val) {
                DEBUGOUT("EEPROM Read Error\n");
                return -E1000_ERR_EEPROM;
            }
            if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) == 0)
                hw->fc = E1000_FC_NONE;
            else if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) ==
                    EEPROM_WORD0F_ASM_DIR)
                hw->fc = E1000_FC_TX_PAUSE;
            else
                hw->fc = E1000_FC_FULL;
            break;
        }
    }

    /* We want to save off the original Flow Control configuration just
     * in case we get disconnected and then reconnected into a different
     * hub or switch with different Flow Control capabilities.
     */
    if (hw->mac_type == e1000_82542_rev2_0)
        hw->fc &= (~E1000_FC_TX_PAUSE);

    if ((hw->mac_type < e1000_82543) && (hw->report_tx_early == 1))
        hw->fc &= (~E1000_FC_RX_PAUSE);

    hw->original_fc = hw->fc;

    DEBUGOUT1("After fix-ups FlowControl is now = %x\n", hw->fc);

    /* Take the 4 bits from EEPROM word 0x0F that determine the initial
     * polarity value for the SW controlled pins, and setup the
     * Extended Device Control reg with that info.
     * This is needed because one of the SW controlled pins is used for
     * signal detection.  So this should be done before e1000_setup_pcs_link()
     * or e1000_phy_setup() is called.
     */
    if (hw->mac_type == e1000_82543) {
        ret_val = e1000_read_eeprom(hw, EEPROM_INIT_CONTROL2_REG,
                                    1, &eeprom_data);
        if (ret_val) {
            DEBUGOUT("EEPROM Read Error\n");
            return -E1000_ERR_EEPROM;
        }
        ctrl_ext = ((eeprom_data & EEPROM_WORD0F_SWPDIO_EXT) <<
                    SWDPIO__EXT_SHIFT);
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
    }

    /* Call the necessary subroutine to configure the link. */
    ret_val = (hw->media_type == e1000_media_type_copper) ?
              e1000_setup_copper_link(hw) :
              e1000_setup_fiber_serdes_link(hw);

    /* Initialize the flow control address, type, and PAUSE timer
     * registers to their default values.  This is done even if flow
     * control is disabled, because it does not hurt anything to
     * initialize these registers.
     */
    DEBUGOUT("Initializing the Flow Control address, type and timer regs\n");

    /* FCAL/H and FCT are hardcoded to standard values in e1000_ich8lan. */
    if (hw->mac_type != e1000_ich8lan) {
        E1000_WRITE_REG(hw, FCT, FLOW_CONTROL_TYPE);
        E1000_WRITE_REG(hw, FCAH, FLOW_CONTROL_ADDRESS_HIGH);
        E1000_WRITE_REG(hw, FCAL, FLOW_CONTROL_ADDRESS_LOW);
    }

    E1000_WRITE_REG(hw, FCTTV, hw->fc_pause_time);

    /* Set the flow control receive threshold registers.  Normally,
     * these registers will be set to a default threshold that may be
     * adjusted later by the driver's runtime code.  However, if the
     * ability to transmit pause frames in not enabled, then these
     * registers will be set to 0.
     */
    if (!(hw->fc & E1000_FC_TX_PAUSE)) {
        E1000_WRITE_REG(hw, FCRTL, 0);
        E1000_WRITE_REG(hw, FCRTH, 0);
    } else {
        /* We need to set up the Receive Threshold high and low water marks
         * as well as (optionally) enabling the transmission of XON frames.
         */
        if (hw->fc_send_xon) {
            E1000_WRITE_REG(hw, FCRTL, (hw->fc_low_water | E1000_FCRTL_XONE));
            E1000_WRITE_REG(hw, FCRTH, hw->fc_high_water);
        } else {
            E1000_WRITE_REG(hw, FCRTL, hw->fc_low_water);
            E1000_WRITE_REG(hw, FCRTH, hw->fc_high_water);
        }
    }
    return ret_val;
}

/******************************************************************************
 * Sets up link for a fiber based or serdes based adapter
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Manipulates Physical Coding Sublayer functions in order to configure
 * link. Assumes the hardware has been previously reset and the transmitter
 * and receiver are not enabled.
 *****************************************************************************/
static int32_t
e1000_setup_fiber_serdes_link(struct e1000_hw *hw)
{
    uint32_t ctrl;
    uint32_t status;
    uint32_t txcw = 0;
    uint32_t i;
    uint32_t signal = 0;
    int32_t ret_val;

    DEBUGFUNC("e1000_setup_fiber_serdes_link");

    /* On 82571 and 82572 Fiber connections, SerDes loopback mode persists
     * until explicitly turned off or a power cycle is performed.  A read to
     * the register does not indicate its status.  Therefore, we ensure
     * loopback mode is disabled during initialization.
     */
    if (hw->mac_type == e1000_82571 || hw->mac_type == e1000_82572)
        E1000_WRITE_REG(hw, SCTL, E1000_DISABLE_SERDES_LOOPBACK);

    /* On adapters with a MAC newer than 82544, SWDP 1 will be
     * set when the optics detect a signal. On older adapters, it will be
     * cleared when there is a signal.  This applies to fiber media only.
     * If we're on serdes media, adjust the output amplitude to value
     * set in the EEPROM.
     */
    ctrl = E1000_READ_REG(hw, CTRL);
    if (hw->media_type == e1000_media_type_fiber)
        signal = (hw->mac_type > e1000_82544) ? E1000_CTRL_SWDPIN1 : 0;

    ret_val = e1000_adjust_serdes_amplitude(hw);
    if (ret_val)
        return ret_val;

    /* Take the link out of reset */
    ctrl &= ~(E1000_CTRL_LRST);

    /* Adjust VCO speed to improve BER performance */
    ret_val = e1000_set_vco_speed(hw);
    if (ret_val)
        return ret_val;

    e1000_config_collision_dist(hw);

    /* Check for a software override of the flow control settings, and setup
     * the device accordingly.  If auto-negotiation is enabled, then software
     * will have to set the "PAUSE" bits to the correct value in the Tranmsit
     * Config Word Register (TXCW) and re-start auto-negotiation.  However, if
     * auto-negotiation is disabled, then software will have to manually
     * configure the two flow control enable bits in the CTRL register.
     *
     * The possible values of the "fc" parameter are:
     *      0:  Flow control is completely disabled
     *      1:  Rx flow control is enabled (we can receive pause frames, but
     *          not send pause frames).
     *      2:  Tx flow control is enabled (we can send pause frames but we do
     *          not support receiving pause frames).
     *      3:  Both Rx and TX flow control (symmetric) are enabled.
     */
    switch (hw->fc) {
    case E1000_FC_NONE:
        /* Flow control is completely disabled by a software over-ride. */
        txcw = (E1000_TXCW_ANE | E1000_TXCW_FD);
        break;
    case E1000_FC_RX_PAUSE:
        /* RX Flow control is enabled and TX Flow control is disabled by a
         * software over-ride. Since there really isn't a way to advertise
         * that we are capable of RX Pause ONLY, we will advertise that we
         * support both symmetric and asymmetric RX PAUSE. Later, we will
         *  disable the adapter's ability to send PAUSE frames.
         */
        txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_PAUSE_MASK);
        break;
    case E1000_FC_TX_PAUSE:
        /* TX Flow control is enabled, and RX Flow control is disabled, by a
         * software over-ride.
         */
        txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_ASM_DIR);
        break;
    case E1000_FC_FULL:
        /* Flow control (both RX and TX) is enabled by a software over-ride. */
        txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_PAUSE_MASK);
        break;
    default:
        DEBUGOUT("Flow control param set incorrectly\n");
        return -E1000_ERR_CONFIG;
        break;
    }

    /* Since auto-negotiation is enabled, take the link out of reset (the link
     * will be in reset, because we previously reset the chip). This will
     * restart auto-negotiation.  If auto-neogtiation is successful then the
     * link-up status bit will be set and the flow control enable bits (RFCE
     * and TFCE) will be set according to their negotiated value.
     */
    DEBUGOUT("Auto-negotiation enabled\n");

    E1000_WRITE_REG(hw, TXCW, txcw);
    E1000_WRITE_REG(hw, CTRL, ctrl);
    E1000_WRITE_FLUSH(hw);

    hw->txcw = txcw;
    msleep(1);

    /* If we have a signal (the cable is plugged in) then poll for a "Link-Up"
     * indication in the Device Status Register.  Time-out if a link isn't
     * seen in 500 milliseconds seconds (Auto-negotiation should complete in
     * less than 500 milliseconds even if the other end is doing it in SW).
     * For internal serdes, we just assume a signal is present, then poll.
     */
    if (hw->media_type == e1000_media_type_internal_serdes ||
       (E1000_READ_REG(hw, CTRL) & E1000_CTRL_SWDPIN1) == signal) {
        DEBUGOUT("Looking for Link\n");
        for (i = 0; i < (LINK_UP_TIMEOUT / 10); i++) {
            msleep(10);
            status = E1000_READ_REG(hw, STATUS);
            if (status & E1000_STATUS_LU) break;
        }
        if (i == (LINK_UP_TIMEOUT / 10)) {
            DEBUGOUT("Never got a valid link from auto-neg!!!\n");
            hw->autoneg_failed = 1;
            /* AutoNeg failed to achieve a link, so we'll call
             * e1000_check_for_link. This routine will force the link up if
             * we detect a signal. This will allow us to communicate with
             * non-autonegotiating link partners.
             */
            ret_val = e1000_check_for_link(hw);
            if (ret_val) {
                DEBUGOUT("Error while checking for link\n");
                return ret_val;
            }
            hw->autoneg_failed = 0;
        } else {
            hw->autoneg_failed = 0;
            DEBUGOUT("Valid Link Found\n");
        }
    } else {
        DEBUGOUT("No Signal Detected\n");
    }
    return E1000_SUCCESS;
}

/******************************************************************************
* Make sure we have a valid PHY and change PHY mode before link setup.
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_copper_link_preconfig(struct e1000_hw *hw)
{
    uint32_t ctrl;
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_copper_link_preconfig");

    ctrl = E1000_READ_REG(hw, CTRL);
    /* With 82543, we need to force speed and duplex on the MAC equal to what
     * the PHY speed and duplex configuration is. In addition, we need to
     * perform a hardware reset on the PHY to take it out of reset.
     */
    if (hw->mac_type > e1000_82543) {
        ctrl |= E1000_CTRL_SLU;
        ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
        E1000_WRITE_REG(hw, CTRL, ctrl);
    } else {
        ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX | E1000_CTRL_SLU);
        E1000_WRITE_REG(hw, CTRL, ctrl);
        ret_val = e1000_phy_hw_reset(hw);
        if (ret_val)
            return ret_val;
    }

    /* Make sure we have a valid PHY */
    ret_val = e1000_detect_gig_phy(hw);
    if (ret_val) {
        DEBUGOUT("Error, did not detect valid phy.\n");
        return ret_val;
    }
    DEBUGOUT1("Phy ID = %x \n", hw->phy_id);

    /* Set PHY to class A mode (if necessary) */
    ret_val = e1000_set_phy_mode(hw);
    if (ret_val)
        return ret_val;

    if ((hw->mac_type == e1000_82545_rev_3) ||
       (hw->mac_type == e1000_82546_rev_3)) {
        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
        phy_data |= 0x00000008;
        ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
    }

    if (hw->mac_type <= e1000_82543 ||
        hw->mac_type == e1000_82541 || hw->mac_type == e1000_82547 ||
        hw->mac_type == e1000_82541_rev_2 || hw->mac_type == e1000_82547_rev_2)
        hw->phy_reset_disable = FALSE;

   return E1000_SUCCESS;
}


/********************************************************************
* Copper link setup for e1000_phy_igp series.
*
* hw - Struct containing variables accessed by shared code
*********************************************************************/
static int32_t
e1000_copper_link_igp_setup(struct e1000_hw *hw)
{
    uint32_t led_ctrl;
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_copper_link_igp_setup");

    if (hw->phy_reset_disable)
        return E1000_SUCCESS;

    ret_val = e1000_phy_reset(hw);
    if (ret_val) {
        DEBUGOUT("Error Resetting the PHY\n");
        return ret_val;
    }

    /* Wait 15ms for MAC to configure PHY from eeprom settings */
    msleep(15);
    if (hw->mac_type != e1000_ich8lan) {
    /* Configure activity LED after PHY reset */
    led_ctrl = E1000_READ_REG(hw, LEDCTL);
    led_ctrl &= IGP_ACTIVITY_LED_MASK;
    led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
    E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
    }

    /* The NVM settings will configure LPLU in D3 for IGP2 and IGP3 PHYs */
    if (hw->phy_type == e1000_phy_igp) {
        /* disable lplu d3 during driver init */
        ret_val = e1000_set_d3_lplu_state(hw, FALSE);
        if (ret_val) {
            DEBUGOUT("Error Disabling LPLU D3\n");
            return ret_val;
        }
    }

    /* disable lplu d0 during driver init */
    ret_val = e1000_set_d0_lplu_state(hw, FALSE);
    if (ret_val) {
        DEBUGOUT("Error Disabling LPLU D0\n");
        return ret_val;
    }
    /* Configure mdi-mdix settings */
    ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
    if (ret_val)
        return ret_val;

    if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
        hw->dsp_config_state = e1000_dsp_config_disabled;
        /* Force MDI for earlier revs of the IGP PHY */
        phy_data &= ~(IGP01E1000_PSCR_AUTO_MDIX | IGP01E1000_PSCR_FORCE_MDI_MDIX);
        hw->mdix = 1;

    } else {
        hw->dsp_config_state = e1000_dsp_config_enabled;
        phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;

        switch (hw->mdix) {
        case 1:
            phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;
            break;
        case 2:
            phy_data |= IGP01E1000_PSCR_FORCE_MDI_MDIX;
            break;
        case 0:
        default:
            phy_data |= IGP01E1000_PSCR_AUTO_MDIX;
            break;
        }
    }
    ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
    if (ret_val)
        return ret_val;

    /* set auto-master slave resolution settings */
    if (hw->autoneg) {
        e1000_ms_type phy_ms_setting = hw->master_slave;

        if (hw->ffe_config_state == e1000_ffe_config_active)
            hw->ffe_config_state = e1000_ffe_config_enabled;

        if (hw->dsp_config_state == e1000_dsp_config_activated)
            hw->dsp_config_state = e1000_dsp_config_enabled;

        /* when autonegotiation advertisment is only 1000Mbps then we
          * should disable SmartSpeed and enable Auto MasterSlave
          * resolution as hardware default. */
        if (hw->autoneg_advertised == ADVERTISE_1000_FULL) {
            /* Disable SmartSpeed */
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                         &phy_data);
            if (ret_val)
                return ret_val;
            phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                          phy_data);
            if (ret_val)
                return ret_val;
            /* Set auto Master/Slave resolution process */
            ret_val = e1000_read_phy_reg(hw, PHY_1000T_CTRL, &phy_data);
            if (ret_val)
                return ret_val;
            phy_data &= ~CR_1000T_MS_ENABLE;
            ret_val = e1000_write_phy_reg(hw, PHY_1000T_CTRL, phy_data);
            if (ret_val)
                return ret_val;
        }

        ret_val = e1000_read_phy_reg(hw, PHY_1000T_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        /* load defaults for future use */
        hw->original_master_slave = (phy_data & CR_1000T_MS_ENABLE) ?
                                        ((phy_data & CR_1000T_MS_VALUE) ?
                                         e1000_ms_force_master :
                                         e1000_ms_force_slave) :
                                         e1000_ms_auto;

        switch (phy_ms_setting) {
        case e1000_ms_force_master:
            phy_data |= (CR_1000T_MS_ENABLE | CR_1000T_MS_VALUE);
            break;
        case e1000_ms_force_slave:
            phy_data |= CR_1000T_MS_ENABLE;
            phy_data &= ~(CR_1000T_MS_VALUE);
            break;
        case e1000_ms_auto:
            phy_data &= ~CR_1000T_MS_ENABLE;
            default:
            break;
        }
        ret_val = e1000_write_phy_reg(hw, PHY_1000T_CTRL, phy_data);
        if (ret_val)
            return ret_val;
    }

    return E1000_SUCCESS;
}

/********************************************************************
* Copper link setup for e1000_phy_gg82563 series.
*
* hw - Struct containing variables accessed by shared code
*********************************************************************/
static int32_t
e1000_copper_link_ggp_setup(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t phy_data;
    uint32_t reg_data;

    DEBUGFUNC("e1000_copper_link_ggp_setup");

    if (!hw->phy_reset_disable) {

        /* Enable CRS on TX for half-duplex operation. */
        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
                                     &phy_data);
        if (ret_val)
            return ret_val;

        phy_data |= GG82563_MSCR_ASSERT_CRS_ON_TX;
        /* Use 25MHz for both link down and 1000BASE-T for Tx clock */
        phy_data |= GG82563_MSCR_TX_CLK_1000MBPS_25MHZ;

        ret_val = e1000_write_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL,
                                      phy_data);
        if (ret_val)
            return ret_val;

        /* Options:
         *   MDI/MDI-X = 0 (default)
         *   0 - Auto for all speeds
         *   1 - MDI mode
         *   2 - MDI-X mode
         *   3 - Auto for 1000Base-T only (MDI-X for 10/100Base-T modes)
         */
        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~GG82563_PSCR_CROSSOVER_MODE_MASK;

        switch (hw->mdix) {
        case 1:
            phy_data |= GG82563_PSCR_CROSSOVER_MODE_MDI;
            break;
        case 2:
            phy_data |= GG82563_PSCR_CROSSOVER_MODE_MDIX;
            break;
        case 0:
        default:
            phy_data |= GG82563_PSCR_CROSSOVER_MODE_AUTO;
            break;
        }

        /* Options:
         *   disable_polarity_correction = 0 (default)
         *       Automatic Correction for Reversed Cable Polarity
         *   0 - Disabled
         *   1 - Enabled
         */
        phy_data &= ~GG82563_PSCR_POLARITY_REVERSAL_DISABLE;
        if (hw->disable_polarity_correction == 1)
            phy_data |= GG82563_PSCR_POLARITY_REVERSAL_DISABLE;
        ret_val = e1000_write_phy_reg(hw, GG82563_PHY_SPEC_CTRL, phy_data);

        if (ret_val)
            return ret_val;

        /* SW Reset the PHY so all changes take effect */
        ret_val = e1000_phy_reset(hw);
        if (ret_val) {
            DEBUGOUT("Error Resetting the PHY\n");
            return ret_val;
        }
    } /* phy_reset_disable */

    if (hw->mac_type == e1000_80003es2lan) {
        /* Bypass RX and TX FIFO's */
        ret_val = e1000_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_FIFO_CTRL,
                                       E1000_KUMCTRLSTA_FIFO_CTRL_RX_BYPASS |
                                       E1000_KUMCTRLSTA_FIFO_CTRL_TX_BYPASS);
        if (ret_val)
            return ret_val;

        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_SPEC_CTRL_2, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~GG82563_PSCR2_REVERSE_AUTO_NEG;
        ret_val = e1000_write_phy_reg(hw, GG82563_PHY_SPEC_CTRL_2, phy_data);

        if (ret_val)
            return ret_val;

        reg_data = E1000_READ_REG(hw, CTRL_EXT);
        reg_data &= ~(E1000_CTRL_EXT_LINK_MODE_MASK);
        E1000_WRITE_REG(hw, CTRL_EXT, reg_data);

        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_PWR_MGMT_CTRL,
                                          &phy_data);
        if (ret_val)
            return ret_val;

        /* Do not init these registers when the HW is in IAMT mode, since the
         * firmware will have already initialized them.  We only initialize
         * them if the HW is not in IAMT mode.
         */
        if (e1000_check_mng_mode(hw) == FALSE) {
            /* Enable Electrical Idle on the PHY */
            phy_data |= GG82563_PMCR_ENABLE_ELECTRICAL_IDLE;
            ret_val = e1000_write_phy_reg(hw, GG82563_PHY_PWR_MGMT_CTRL,
                                          phy_data);
            if (ret_val)
                return ret_val;

            ret_val = e1000_read_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;
            ret_val = e1000_write_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL,
                                          phy_data);

            if (ret_val)
                return ret_val;
        }

        /* Workaround: Disable padding in Kumeran interface in the MAC
         * and in the PHY to avoid CRC errors.
         */
        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_INBAND_CTRL,
                                     &phy_data);
        if (ret_val)
            return ret_val;
        phy_data |= GG82563_ICR_DIS_PADDING;
        ret_val = e1000_write_phy_reg(hw, GG82563_PHY_INBAND_CTRL,
                                      phy_data);
        if (ret_val)
            return ret_val;
    }

    return E1000_SUCCESS;
}

/********************************************************************
* Copper link setup for e1000_phy_m88 series.
*
* hw - Struct containing variables accessed by shared code
*********************************************************************/
static int32_t
e1000_copper_link_mgp_setup(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_copper_link_mgp_setup");

    if (hw->phy_reset_disable)
        return E1000_SUCCESS;

    /* Enable CRS on TX. This must be set for half-duplex operation. */
    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;

    /* Options:
     *   MDI/MDI-X = 0 (default)
     *   0 - Auto for all speeds
     *   1 - MDI mode
     *   2 - MDI-X mode
     *   3 - Auto for 1000Base-T only (MDI-X for 10/100Base-T modes)
     */
    phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;

    switch (hw->mdix) {
    case 1:
        phy_data |= M88E1000_PSCR_MDI_MANUAL_MODE;
        break;
    case 2:
        phy_data |= M88E1000_PSCR_MDIX_MANUAL_MODE;
        break;
    case 3:
        phy_data |= M88E1000_PSCR_AUTO_X_1000T;
        break;
    case 0:
    default:
        phy_data |= M88E1000_PSCR_AUTO_X_MODE;
        break;
    }

    /* Options:
     *   disable_polarity_correction = 0 (default)
     *       Automatic Correction for Reversed Cable Polarity
     *   0 - Disabled
     *   1 - Enabled
     */
    phy_data &= ~M88E1000_PSCR_POLARITY_REVERSAL;
    if (hw->disable_polarity_correction == 1)
        phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
    if (ret_val)
        return ret_val;

    if (hw->phy_revision < M88E1011_I_REV_4) {
        /* Force TX_CLK in the Extended PHY Specific Control Register
         * to 25MHz clock.
         */
        ret_val = e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data |= M88E1000_EPSCR_TX_CLK_25;

        if ((hw->phy_revision == E1000_REVISION_2) &&
            (hw->phy_id == M88E1111_I_PHY_ID)) {
            /* Vidalia Phy, set the downshift counter to 5x */
            phy_data &= ~(M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK);
            phy_data |= M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X;
            ret_val = e1000_write_phy_reg(hw,
                                        M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
            if (ret_val)
                return ret_val;
        } else {
            /* Configure Master and Slave downshift values */
            phy_data &= ~(M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK |
                              M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK);
            phy_data |= (M88E1000_EPSCR_MASTER_DOWNSHIFT_1X |
                             M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X);
            ret_val = e1000_write_phy_reg(hw,
                                        M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
            if (ret_val)
               return ret_val;
        }
    }

    /* SW Reset the PHY so all changes take effect */
    ret_val = e1000_phy_reset(hw);
    if (ret_val) {
        DEBUGOUT("Error Resetting the PHY\n");
        return ret_val;
    }

   return E1000_SUCCESS;
}

/********************************************************************
* Setup auto-negotiation and flow control advertisements,
* and then perform auto-negotiation.
*
* hw - Struct containing variables accessed by shared code
*********************************************************************/
static int32_t
e1000_copper_link_autoneg(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_copper_link_autoneg");

    /* Perform some bounds checking on the hw->autoneg_advertised
     * parameter.  If this variable is zero, then set it to the default.
     */
    hw->autoneg_advertised &= AUTONEG_ADVERTISE_SPEED_DEFAULT;

    /* If autoneg_advertised is zero, we assume it was not defaulted
     * by the calling code so we set to advertise full capability.
     */
    if (hw->autoneg_advertised == 0)
        hw->autoneg_advertised = AUTONEG_ADVERTISE_SPEED_DEFAULT;

    /* IFE phy only supports 10/100 */
    if (hw->phy_type == e1000_phy_ife)
        hw->autoneg_advertised &= AUTONEG_ADVERTISE_10_100_ALL;

    DEBUGOUT("Reconfiguring auto-neg advertisement params\n");
    ret_val = e1000_phy_setup_autoneg(hw);
    if (ret_val) {
        DEBUGOUT("Error Setting up Auto-Negotiation\n");
        return ret_val;
    }
    DEBUGOUT("Restarting Auto-Neg\n");

    /* Restart auto-negotiation by setting the Auto Neg Enable bit and
     * the Auto Neg Restart bit in the PHY control register.
     */
    ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_data |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
    ret_val = e1000_write_phy_reg(hw, PHY_CTRL, phy_data);
    if (ret_val)
        return ret_val;

    /* Does the user want to wait for Auto-Neg to complete here, or
     * check at a later time (for example, callback routine).
     */
    if (hw->wait_autoneg_complete) {
        ret_val = e1000_wait_autoneg(hw);
        if (ret_val) {
            DEBUGOUT("Error while waiting for autoneg to complete\n");
            return ret_val;
        }
    }

    hw->get_link_status = TRUE;

    return E1000_SUCCESS;
}

/******************************************************************************
* Config the MAC and the PHY after link is up.
*   1) Set up the MAC to the current PHY speed/duplex
*      if we are on 82543.  If we
*      are on newer silicon, we only need to configure
*      collision distance in the Transmit Control Register.
*   2) Set up flow control on the MAC to that established with
*      the link partner.
*   3) Config DSP to improve Gigabit link quality for some PHY revisions.
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_copper_link_postconfig(struct e1000_hw *hw)
{
    int32_t ret_val;
    DEBUGFUNC("e1000_copper_link_postconfig");

    if (hw->mac_type >= e1000_82544) {
        e1000_config_collision_dist(hw);
    } else {
        ret_val = e1000_config_mac_to_phy(hw);
        if (ret_val) {
            DEBUGOUT("Error configuring MAC to PHY settings\n");
            return ret_val;
        }
    }
    ret_val = e1000_config_fc_after_link_up(hw);
    if (ret_val) {
        DEBUGOUT("Error Configuring Flow Control\n");
        return ret_val;
    }

    /* Config DSP to improve Giga link quality */
    if (hw->phy_type == e1000_phy_igp) {
        ret_val = e1000_config_dsp_after_link_change(hw, TRUE);
        if (ret_val) {
            DEBUGOUT("Error Configuring DSP after link up\n");
            return ret_val;
        }
    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Detects which PHY is present and setup the speed and duplex
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_setup_copper_link(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t i;
    uint16_t phy_data;
    uint16_t reg_data;

    DEBUGFUNC("e1000_setup_copper_link");

    switch (hw->mac_type) {
    case e1000_80003es2lan:
    case e1000_ich8lan:
        /* Set the mac to wait the maximum time between each
         * iteration and increase the max iterations when
         * polling the phy; this fixes erroneous timeouts at 10Mbps. */
        ret_val = e1000_write_kmrn_reg(hw, GG82563_REG(0x34, 4), 0xFFFF);
        if (ret_val)
            return ret_val;
        ret_val = e1000_read_kmrn_reg(hw, GG82563_REG(0x34, 9), &reg_data);
        if (ret_val)
            return ret_val;
        reg_data |= 0x3F;
        ret_val = e1000_write_kmrn_reg(hw, GG82563_REG(0x34, 9), reg_data);
        if (ret_val)
            return ret_val;
    default:
        break;
    }

    /* Check if it is a valid PHY and set PHY mode if necessary. */
    ret_val = e1000_copper_link_preconfig(hw);
    if (ret_val)
        return ret_val;

    switch (hw->mac_type) {
    case e1000_80003es2lan:
        /* Kumeran registers are written-only */
        reg_data = E1000_KUMCTRLSTA_INB_CTRL_LINK_STATUS_TX_TIMEOUT_DEFAULT;
        reg_data |= E1000_KUMCTRLSTA_INB_CTRL_DIS_PADDING;
        ret_val = e1000_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_INB_CTRL,
                                       reg_data);
        if (ret_val)
            return ret_val;
        break;
    default:
        break;
    }

    if (hw->phy_type == e1000_phy_igp ||
        hw->phy_type == e1000_phy_igp_3 ||
        hw->phy_type == e1000_phy_igp_2) {
        ret_val = e1000_copper_link_igp_setup(hw);
        if (ret_val)
            return ret_val;
    } else if (hw->phy_type == e1000_phy_m88) {
        ret_val = e1000_copper_link_mgp_setup(hw);
        if (ret_val)
            return ret_val;
    } else if (hw->phy_type == e1000_phy_gg82563) {
        ret_val = e1000_copper_link_ggp_setup(hw);
        if (ret_val)
            return ret_val;
    }

    if (hw->autoneg) {
        /* Setup autoneg and flow control advertisement
          * and perform autonegotiation */
        ret_val = e1000_copper_link_autoneg(hw);
        if (ret_val)
            return ret_val;
    } else {
        /* PHY will be set to 10H, 10F, 100H,or 100F
          * depending on value from forced_speed_duplex. */
        DEBUGOUT("Forcing speed and duplex\n");
        ret_val = e1000_phy_force_speed_duplex(hw);
        if (ret_val) {
            DEBUGOUT("Error Forcing Speed and Duplex\n");
            return ret_val;
        }
    }

    /* Check link status. Wait up to 100 microseconds for link to become
     * valid.
     */
    for (i = 0; i < 10; i++) {
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;

        if (phy_data & MII_SR_LINK_STATUS) {
            /* Config the MAC and PHY after link is up */
            ret_val = e1000_copper_link_postconfig(hw);
            if (ret_val)
                return ret_val;

            DEBUGOUT("Valid link established!!!\n");
            return E1000_SUCCESS;
        }
        udelay(10);
    }

    DEBUGOUT("Unable to establish link!!!\n");
    return E1000_SUCCESS;
}

/******************************************************************************
* Configure the MAC-to-PHY interface for 10/100Mbps
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_configure_kmrn_for_10_100(struct e1000_hw *hw, uint16_t duplex)
{
    int32_t ret_val = E1000_SUCCESS;
    uint32_t tipg;
    uint16_t reg_data;

    DEBUGFUNC("e1000_configure_kmrn_for_10_100");

    reg_data = E1000_KUMCTRLSTA_HD_CTRL_10_100_DEFAULT;
    ret_val = e1000_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL,
                                   reg_data);
    if (ret_val)
        return ret_val;

    /* Configure Transmit Inter-Packet Gap */
    tipg = E1000_READ_REG(hw, TIPG);
    tipg &= ~E1000_TIPG_IPGT_MASK;
    tipg |= DEFAULT_80003ES2LAN_TIPG_IPGT_10_100;
    E1000_WRITE_REG(hw, TIPG, tipg);

    ret_val = e1000_read_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, &reg_data);

    if (ret_val)
        return ret_val;

    if (duplex == HALF_DUPLEX)
        reg_data |= GG82563_KMCR_PASS_FALSE_CARRIER;
    else
        reg_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;

    ret_val = e1000_write_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, reg_data);

    return ret_val;
}

static int32_t
e1000_configure_kmrn_for_1000(struct e1000_hw *hw)
{
    int32_t ret_val = E1000_SUCCESS;
    uint16_t reg_data;
    uint32_t tipg;

    DEBUGFUNC("e1000_configure_kmrn_for_1000");

    reg_data = E1000_KUMCTRLSTA_HD_CTRL_1000_DEFAULT;
    ret_val = e1000_write_kmrn_reg(hw, E1000_KUMCTRLSTA_OFFSET_HD_CTRL,
                                   reg_data);
    if (ret_val)
        return ret_val;

    /* Configure Transmit Inter-Packet Gap */
    tipg = E1000_READ_REG(hw, TIPG);
    tipg &= ~E1000_TIPG_IPGT_MASK;
    tipg |= DEFAULT_80003ES2LAN_TIPG_IPGT_1000;
    E1000_WRITE_REG(hw, TIPG, tipg);

    ret_val = e1000_read_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, &reg_data);

    if (ret_val)
        return ret_val;

    reg_data &= ~GG82563_KMCR_PASS_FALSE_CARRIER;
    ret_val = e1000_write_phy_reg(hw, GG82563_PHY_KMRN_MODE_CTRL, reg_data);

    return ret_val;
}

/******************************************************************************
* Configures PHY autoneg and flow control advertisement settings
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
int32_t
e1000_phy_setup_autoneg(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t mii_autoneg_adv_reg;
    uint16_t mii_1000t_ctrl_reg;

    DEBUGFUNC("e1000_phy_setup_autoneg");

    /* Read the MII Auto-Neg Advertisement Register (Address 4). */
    ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
    if (ret_val)
        return ret_val;

    if (hw->phy_type != e1000_phy_ife) {
        /* Read the MII 1000Base-T Control Register (Address 9). */
        ret_val = e1000_read_phy_reg(hw, PHY_1000T_CTRL, &mii_1000t_ctrl_reg);
        if (ret_val)
            return ret_val;
    } else
        mii_1000t_ctrl_reg=0;

    /* Need to parse both autoneg_advertised and fc and set up
     * the appropriate PHY registers.  First we will parse for
     * autoneg_advertised software override.  Since we can advertise
     * a plethora of combinations, we need to check each bit
     * individually.
     */

    /* First we clear all the 10/100 mb speed bits in the Auto-Neg
     * Advertisement Register (Address 4) and the 1000 mb speed bits in
     * the  1000Base-T Control Register (Address 9).
     */
    mii_autoneg_adv_reg &= ~REG4_SPEED_MASK;
    mii_1000t_ctrl_reg &= ~REG9_SPEED_MASK;

    DEBUGOUT1("autoneg_advertised %x\n", hw->autoneg_advertised);

    /* Do we want to advertise 10 Mb Half Duplex? */
    if (hw->autoneg_advertised & ADVERTISE_10_HALF) {
        DEBUGOUT("Advertise 10mb Half duplex\n");
        mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
    }

    /* Do we want to advertise 10 Mb Full Duplex? */
    if (hw->autoneg_advertised & ADVERTISE_10_FULL) {
        DEBUGOUT("Advertise 10mb Full duplex\n");
        mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
    }

    /* Do we want to advertise 100 Mb Half Duplex? */
    if (hw->autoneg_advertised & ADVERTISE_100_HALF) {
        DEBUGOUT("Advertise 100mb Half duplex\n");
        mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
    }

    /* Do we want to advertise 100 Mb Full Duplex? */
    if (hw->autoneg_advertised & ADVERTISE_100_FULL) {
        DEBUGOUT("Advertise 100mb Full duplex\n");
        mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
    }

    /* We do not allow the Phy to advertise 1000 Mb Half Duplex */
    if (hw->autoneg_advertised & ADVERTISE_1000_HALF) {
        DEBUGOUT("Advertise 1000mb Half duplex requested, request denied!\n");
    }

    /* Do we want to advertise 1000 Mb Full Duplex? */
    if (hw->autoneg_advertised & ADVERTISE_1000_FULL) {
        DEBUGOUT("Advertise 1000mb Full duplex\n");
        mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
        if (hw->phy_type == e1000_phy_ife) {
            DEBUGOUT("e1000_phy_ife is a 10/100 PHY. Gigabit speed is not supported.\n");
        }
    }

    /* Check for a software override of the flow control settings, and
     * setup the PHY advertisement registers accordingly.  If
     * auto-negotiation is enabled, then software will have to set the
     * "PAUSE" bits to the correct value in the Auto-Negotiation
     * Advertisement Register (PHY_AUTONEG_ADV) and re-start auto-negotiation.
     *
     * The possible values of the "fc" parameter are:
     *      0:  Flow control is completely disabled
     *      1:  Rx flow control is enabled (we can receive pause frames
     *          but not send pause frames).
     *      2:  Tx flow control is enabled (we can send pause frames
     *          but we do not support receiving pause frames).
     *      3:  Both Rx and TX flow control (symmetric) are enabled.
     *  other:  No software override.  The flow control configuration
     *          in the EEPROM is used.
     */
    switch (hw->fc) {
    case E1000_FC_NONE: /* 0 */
        /* Flow control (RX & TX) is completely disabled by a
         * software over-ride.
         */
        mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
        break;
    case E1000_FC_RX_PAUSE: /* 1 */
        /* RX Flow control is enabled, and TX Flow control is
         * disabled, by a software over-ride.
         */
        /* Since there really isn't a way to advertise that we are
         * capable of RX Pause ONLY, we will advertise that we
         * support both symmetric and asymmetric RX PAUSE.  Later
         * (in e1000_config_fc_after_link_up) we will disable the
         *hw's ability to send PAUSE frames.
         */
        mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
        break;
    case E1000_FC_TX_PAUSE: /* 2 */
        /* TX Flow control is enabled, and RX Flow control is
         * disabled, by a software over-ride.
         */
        mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
        mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
        break;
    case E1000_FC_FULL: /* 3 */
        /* Flow control (both RX and TX) is enabled by a software
         * over-ride.
         */
        mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
        break;
    default:
        DEBUGOUT("Flow control param set incorrectly\n");
        return -E1000_ERR_CONFIG;
    }

    ret_val = e1000_write_phy_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
    if (ret_val)
        return ret_val;

    DEBUGOUT1("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

    if (hw->phy_type != e1000_phy_ife) {
        ret_val = e1000_write_phy_reg(hw, PHY_1000T_CTRL, mii_1000t_ctrl_reg);
        if (ret_val)
            return ret_val;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Force PHY speed and duplex settings to hw->forced_speed_duplex
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_phy_force_speed_duplex(struct e1000_hw *hw)
{
    uint32_t ctrl;
    int32_t ret_val;
    uint16_t mii_ctrl_reg;
    uint16_t mii_status_reg;
    uint16_t phy_data;
    uint16_t i;

    DEBUGFUNC("e1000_phy_force_speed_duplex");

    /* Turn off Flow control if we are forcing speed and duplex. */
    hw->fc = E1000_FC_NONE;

    DEBUGOUT1("hw->fc = %d\n", hw->fc);

    /* Read the Device Control Register. */
    ctrl = E1000_READ_REG(hw, CTRL);

    /* Set the bits to Force Speed and Duplex in the Device Ctrl Reg. */
    ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
    ctrl &= ~(DEVICE_SPEED_MASK);

    /* Clear the Auto Speed Detect Enable bit. */
    ctrl &= ~E1000_CTRL_ASDE;

    /* Read the MII Control Register. */
    ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &mii_ctrl_reg);
    if (ret_val)
        return ret_val;

    /* We need to disable autoneg in order to force link and duplex. */

    mii_ctrl_reg &= ~MII_CR_AUTO_NEG_EN;

    /* Are we forcing Full or Half Duplex? */
    if (hw->forced_speed_duplex == e1000_100_full ||
        hw->forced_speed_duplex == e1000_10_full) {
        /* We want to force full duplex so we SET the full duplex bits in the
         * Device and MII Control Registers.
         */
        ctrl |= E1000_CTRL_FD;
        mii_ctrl_reg |= MII_CR_FULL_DUPLEX;
        DEBUGOUT("Full Duplex\n");
    } else {
        /* We want to force half duplex so we CLEAR the full duplex bits in
         * the Device and MII Control Registers.
         */
        ctrl &= ~E1000_CTRL_FD;
        mii_ctrl_reg &= ~MII_CR_FULL_DUPLEX;
        DEBUGOUT("Half Duplex\n");
    }

    /* Are we forcing 100Mbps??? */
    if (hw->forced_speed_duplex == e1000_100_full ||
       hw->forced_speed_duplex == e1000_100_half) {
        /* Set the 100Mb bit and turn off the 1000Mb and 10Mb bits. */
        ctrl |= E1000_CTRL_SPD_100;
        mii_ctrl_reg |= MII_CR_SPEED_100;
        mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_10);
        DEBUGOUT("Forcing 100mb ");
    } else {
        /* Set the 10Mb bit and turn off the 1000Mb and 100Mb bits. */
        ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
        mii_ctrl_reg |= MII_CR_SPEED_10;
        mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_100);
        DEBUGOUT("Forcing 10mb ");
    }

    e1000_config_collision_dist(hw);

    /* Write the configured values back to the Device Control Reg. */
    E1000_WRITE_REG(hw, CTRL, ctrl);

    if ((hw->phy_type == e1000_phy_m88) ||
        (hw->phy_type == e1000_phy_gg82563)) {
        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        /* Clear Auto-Crossover to force MDI manually. M88E1000 requires MDI
         * forced whenever speed are duplex are forced.
         */
        phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;
        ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
        if (ret_val)
            return ret_val;

        DEBUGOUT1("M88E1000 PSCR: %x \n", phy_data);

        /* Need to reset the PHY or these changes will be ignored */
        mii_ctrl_reg |= MII_CR_RESET;

    /* Disable MDI-X support for 10/100 */
    } else if (hw->phy_type == e1000_phy_ife) {
        ret_val = e1000_read_phy_reg(hw, IFE_PHY_MDIX_CONTROL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~IFE_PMC_AUTO_MDIX;
        phy_data &= ~IFE_PMC_FORCE_MDIX;

        ret_val = e1000_write_phy_reg(hw, IFE_PHY_MDIX_CONTROL, phy_data);
        if (ret_val)
            return ret_val;

    } else {
        /* Clear Auto-Crossover to force MDI manually.  IGP requires MDI
         * forced whenever speed or duplex are forced.
         */
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;
        phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;

        ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
        if (ret_val)
            return ret_val;
    }

    /* Write back the modified PHY MII control register. */
    ret_val = e1000_write_phy_reg(hw, PHY_CTRL, mii_ctrl_reg);
    if (ret_val)
        return ret_val;

    udelay(1);

    /* The wait_autoneg_complete flag may be a little misleading here.
     * Since we are forcing speed and duplex, Auto-Neg is not enabled.
     * But we do want to delay for a period while forcing only so we
     * don't generate false No Link messages.  So we will wait here
     * only if the user has set wait_autoneg_complete to 1, which is
     * the default.
     */
    if (hw->wait_autoneg_complete) {
        /* We will wait for autoneg to complete. */
        DEBUGOUT("Waiting for forced speed/duplex link.\n");
        mii_status_reg = 0;

        /* We will wait for autoneg to complete or 4.5 seconds to expire. */
        for (i = PHY_FORCE_TIME; i > 0; i--) {
            /* Read the MII Status Register and wait for Auto-Neg Complete bit
             * to be set.
             */
            ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
            if (ret_val)
                return ret_val;

            ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
            if (ret_val)
                return ret_val;

            if (mii_status_reg & MII_SR_LINK_STATUS) break;
            msleep(100);
        }
        if ((i == 0) &&
           ((hw->phy_type == e1000_phy_m88) ||
            (hw->phy_type == e1000_phy_gg82563))) {
            /* We didn't get link.  Reset the DSP and wait again for link. */
            ret_val = e1000_phy_reset_dsp(hw);
            if (ret_val) {
                DEBUGOUT("Error Resetting PHY DSP\n");
                return ret_val;
            }
        }
        /* This loop will early-out if the link condition has been met.  */
        for (i = PHY_FORCE_TIME; i > 0; i--) {
            if (mii_status_reg & MII_SR_LINK_STATUS) break;
            msleep(100);
            /* Read the MII Status Register and wait for Auto-Neg Complete bit
             * to be set.
             */
            ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
            if (ret_val)
                return ret_val;

            ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
            if (ret_val)
                return ret_val;
        }
    }

    if (hw->phy_type == e1000_phy_m88) {
        /* Because we reset the PHY above, we need to re-force TX_CLK in the
         * Extended PHY Specific Control Register to 25MHz clock.  This value
         * defaults back to a 2.5MHz clock when the PHY is reset.
         */
        ret_val = e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data |= M88E1000_EPSCR_TX_CLK_25;
        ret_val = e1000_write_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, phy_data);
        if (ret_val)
            return ret_val;

        /* In addition, because of the s/w reset above, we need to enable CRS on
         * TX.  This must be set for both full and half duplex operation.
         */
        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
        ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
        if (ret_val)
            return ret_val;

        if ((hw->mac_type == e1000_82544 || hw->mac_type == e1000_82543) &&
            (!hw->autoneg) && (hw->forced_speed_duplex == e1000_10_full ||
             hw->forced_speed_duplex == e1000_10_half)) {
            ret_val = e1000_polarity_reversal_workaround(hw);
            if (ret_val)
                return ret_val;
        }
    } else if (hw->phy_type == e1000_phy_gg82563) {
        /* The TX_CLK of the Extended PHY Specific Control Register defaults
         * to 2.5MHz on a reset.  We need to re-force it back to 25MHz, if
         * we're not in a forced 10/duplex configuration. */
        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~GG82563_MSCR_TX_CLK_MASK;
        if ((hw->forced_speed_duplex == e1000_10_full) ||
            (hw->forced_speed_duplex == e1000_10_half))
            phy_data |= GG82563_MSCR_TX_CLK_10MBPS_2_5MHZ;
        else
            phy_data |= GG82563_MSCR_TX_CLK_100MBPS_25MHZ;

        /* Also due to the reset, we need to enable CRS on Tx. */
        phy_data |= GG82563_MSCR_ASSERT_CRS_ON_TX;

        ret_val = e1000_write_phy_reg(hw, GG82563_PHY_MAC_SPEC_CTRL, phy_data);
        if (ret_val)
            return ret_val;
    }
    return E1000_SUCCESS;
}

/******************************************************************************
* Sets the collision distance in the Transmit Control register
*
* hw - Struct containing variables accessed by shared code
*
* Link should have been established previously. Reads the speed and duplex
* information from the Device Status register.
******************************************************************************/
void
e1000_config_collision_dist(struct e1000_hw *hw)
{
    uint32_t tctl, coll_dist;

    DEBUGFUNC("e1000_config_collision_dist");

    if (hw->mac_type < e1000_82543)
        coll_dist = E1000_COLLISION_DISTANCE_82542;
    else
        coll_dist = E1000_COLLISION_DISTANCE;

    tctl = E1000_READ_REG(hw, TCTL);

    tctl &= ~E1000_TCTL_COLD;
    tctl |= coll_dist << E1000_COLD_SHIFT;

    E1000_WRITE_REG(hw, TCTL, tctl);
    E1000_WRITE_FLUSH(hw);
}

/******************************************************************************
* Sets MAC speed and duplex settings to reflect the those in the PHY
*
* hw - Struct containing variables accessed by shared code
* mii_reg - data to write to the MII control register
*
* The contents of the PHY register containing the needed information need to
* be passed in.
******************************************************************************/
static int32_t
e1000_config_mac_to_phy(struct e1000_hw *hw)
{
    uint32_t ctrl;
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_config_mac_to_phy");

    /* 82544 or newer MAC, Auto Speed Detection takes care of
    * MAC speed/duplex configuration.*/
    if (hw->mac_type >= e1000_82544)
        return E1000_SUCCESS;

    /* Read the Device Control Register and set the bits to Force Speed
     * and Duplex.
     */
    ctrl = E1000_READ_REG(hw, CTRL);
    ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
    ctrl &= ~(E1000_CTRL_SPD_SEL | E1000_CTRL_ILOS);

    /* Set up duplex in the Device Control and Transmit Control
     * registers depending on negotiated values.
     */
    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
    if (ret_val)
        return ret_val;

    if (phy_data & M88E1000_PSSR_DPLX)
        ctrl |= E1000_CTRL_FD;
    else
        ctrl &= ~E1000_CTRL_FD;

    e1000_config_collision_dist(hw);

    /* Set up speed in the Device Control register depending on
     * negotiated values.
     */
    if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS)
        ctrl |= E1000_CTRL_SPD_1000;
    else if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_100MBS)
        ctrl |= E1000_CTRL_SPD_100;

    /* Write the configured values back to the Device Control Reg. */
    E1000_WRITE_REG(hw, CTRL, ctrl);
    return E1000_SUCCESS;
}

/******************************************************************************
 * Forces the MAC's flow control settings.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sets the TFCE and RFCE bits in the device control register to reflect
 * the adapter settings. TFCE and RFCE need to be explicitly set by
 * software when a Copper PHY is used because autonegotiation is managed
 * by the PHY rather than the MAC. Software must also configure these
 * bits when link is forced on a fiber connection.
 *****************************************************************************/
int32_t
e1000_force_mac_fc(struct e1000_hw *hw)
{
    uint32_t ctrl;

    DEBUGFUNC("e1000_force_mac_fc");

    /* Get the current configuration of the Device Control Register */
    ctrl = E1000_READ_REG(hw, CTRL);

    /* Because we didn't get link via the internal auto-negotiation
     * mechanism (we either forced link or we got link via PHY
     * auto-neg), we have to manually enable/disable transmit an
     * receive flow control.
     *
     * The "Case" statement below enables/disable flow control
     * according to the "hw->fc" parameter.
     *
     * The possible values of the "fc" parameter are:
     *      0:  Flow control is completely disabled
     *      1:  Rx flow control is enabled (we can receive pause
     *          frames but not send pause frames).
     *      2:  Tx flow control is enabled (we can send pause frames
     *          frames but we do not receive pause frames).
     *      3:  Both Rx and TX flow control (symmetric) is enabled.
     *  other:  No other values should be possible at this point.
     */

    switch (hw->fc) {
    case E1000_FC_NONE:
        ctrl &= (~(E1000_CTRL_TFCE | E1000_CTRL_RFCE));
        break;
    case E1000_FC_RX_PAUSE:
        ctrl &= (~E1000_CTRL_TFCE);
        ctrl |= E1000_CTRL_RFCE;
        break;
    case E1000_FC_TX_PAUSE:
        ctrl &= (~E1000_CTRL_RFCE);
        ctrl |= E1000_CTRL_TFCE;
        break;
    case E1000_FC_FULL:
        ctrl |= (E1000_CTRL_TFCE | E1000_CTRL_RFCE);
        break;
    default:
        DEBUGOUT("Flow control param set incorrectly\n");
        return -E1000_ERR_CONFIG;
    }

    /* Disable TX Flow Control for 82542 (rev 2.0) */
    if (hw->mac_type == e1000_82542_rev2_0)
        ctrl &= (~E1000_CTRL_TFCE);

    E1000_WRITE_REG(hw, CTRL, ctrl);
    return E1000_SUCCESS;
}

/******************************************************************************
 * Configures flow control settings after link is established
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Should be called immediately after a valid link has been established.
 * Forces MAC flow control settings if link was forced. When in MII/GMII mode
 * and autonegotiation is enabled, the MAC flow control settings will be set
 * based on the flow control negotiated by the PHY. In TBI mode, the TFCE
 * and RFCE bits will be automaticaly set to the negotiated flow control mode.
 *****************************************************************************/
static int32_t
e1000_config_fc_after_link_up(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t mii_status_reg;
    uint16_t mii_nway_adv_reg;
    uint16_t mii_nway_lp_ability_reg;
    uint16_t speed;
    uint16_t duplex;

    DEBUGFUNC("e1000_config_fc_after_link_up");

    /* Check for the case where we have fiber media and auto-neg failed
     * so we had to force link.  In this case, we need to force the
     * configuration of the MAC to match the "fc" parameter.
     */
    if (((hw->media_type == e1000_media_type_fiber) && (hw->autoneg_failed)) ||
        ((hw->media_type == e1000_media_type_internal_serdes) &&
         (hw->autoneg_failed)) ||
        ((hw->media_type == e1000_media_type_copper) && (!hw->autoneg))) {
        ret_val = e1000_force_mac_fc(hw);
        if (ret_val) {
            DEBUGOUT("Error forcing flow control settings\n");
            return ret_val;
        }
    }

    /* Check for the case where we have copper media and auto-neg is
     * enabled.  In this case, we need to check and see if Auto-Neg
     * has completed, and if so, how the PHY and link partner has
     * flow control configured.
     */
    if ((hw->media_type == e1000_media_type_copper) && hw->autoneg) {
        /* Read the MII Status Register and check to see if AutoNeg
         * has completed.  We read this twice because this reg has
         * some "sticky" (latched) bits.
         */
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;

        if (mii_status_reg & MII_SR_AUTONEG_COMPLETE) {
            /* The AutoNeg process has completed, so we now need to
             * read both the Auto Negotiation Advertisement Register
             * (Address 4) and the Auto_Negotiation Base Page Ability
             * Register (Address 5) to determine how flow control was
             * negotiated.
             */
            ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_ADV,
                                         &mii_nway_adv_reg);
            if (ret_val)
                return ret_val;
            ret_val = e1000_read_phy_reg(hw, PHY_LP_ABILITY,
                                         &mii_nway_lp_ability_reg);
            if (ret_val)
                return ret_val;

            /* Two bits in the Auto Negotiation Advertisement Register
             * (Address 4) and two bits in the Auto Negotiation Base
             * Page Ability Register (Address 5) determine flow control
             * for both the PHY and the link partner.  The following
             * table, taken out of the IEEE 802.3ab/D6.0 dated March 25,
             * 1999, describes these PAUSE resolution bits and how flow
             * control is determined based upon these settings.
             * NOTE:  DC = Don't Care
             *
             *   LOCAL DEVICE  |   LINK PARTNER
             * PAUSE | ASM_DIR | PAUSE | ASM_DIR | NIC Resolution
             *-------|---------|-------|---------|--------------------
             *   0   |    0    |  DC   |   DC    | E1000_FC_NONE
             *   0   |    1    |   0   |   DC    | E1000_FC_NONE
             *   0   |    1    |   1   |    0    | E1000_FC_NONE
             *   0   |    1    |   1   |    1    | E1000_FC_TX_PAUSE
             *   1   |    0    |   0   |   DC    | E1000_FC_NONE
             *   1   |   DC    |   1   |   DC    | E1000_FC_FULL
             *   1   |    1    |   0   |    0    | E1000_FC_NONE
             *   1   |    1    |   0   |    1    | E1000_FC_RX_PAUSE
             *
             */
            /* Are both PAUSE bits set to 1?  If so, this implies
             * Symmetric Flow Control is enabled at both ends.  The
             * ASM_DIR bits are irrelevant per the spec.
             *
             * For Symmetric Flow Control:
             *
             *   LOCAL DEVICE  |   LINK PARTNER
             * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
             *-------|---------|-------|---------|--------------------
             *   1   |   DC    |   1   |   DC    | E1000_FC_FULL
             *
             */
            if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
                (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE)) {
                /* Now we need to check if the user selected RX ONLY
                 * of pause frames.  In this case, we had to advertise
                 * FULL flow control because we could not advertise RX
                 * ONLY. Hence, we must now check to see if we need to
                 * turn OFF  the TRANSMISSION of PAUSE frames.
                 */
                if (hw->original_fc == E1000_FC_FULL) {
                    hw->fc = E1000_FC_FULL;
                    DEBUGOUT("Flow Control = FULL.\n");
                } else {
                    hw->fc = E1000_FC_RX_PAUSE;
                    DEBUGOUT("Flow Control = RX PAUSE frames only.\n");
                }
            }
            /* For receiving PAUSE frames ONLY.
             *
             *   LOCAL DEVICE  |   LINK PARTNER
             * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
             *-------|---------|-------|---------|--------------------
             *   0   |    1    |   1   |    1    | E1000_FC_TX_PAUSE
             *
             */
            else if (!(mii_nway_adv_reg & NWAY_AR_PAUSE) &&
                     (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
                     (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
                     (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
                hw->fc = E1000_FC_TX_PAUSE;
                DEBUGOUT("Flow Control = TX PAUSE frames only.\n");
            }
            /* For transmitting PAUSE frames ONLY.
             *
             *   LOCAL DEVICE  |   LINK PARTNER
             * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
             *-------|---------|-------|---------|--------------------
             *   1   |    1    |   0   |    1    | E1000_FC_RX_PAUSE
             *
             */
            else if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
                     (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
                     !(mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
                     (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
                hw->fc = E1000_FC_RX_PAUSE;
                DEBUGOUT("Flow Control = RX PAUSE frames only.\n");
            }
            /* Per the IEEE spec, at this point flow control should be
             * disabled.  However, we want to consider that we could
             * be connected to a legacy switch that doesn't advertise
             * desired flow control, but can be forced on the link
             * partner.  So if we advertised no flow control, that is
             * what we will resolve to.  If we advertised some kind of
             * receive capability (Rx Pause Only or Full Flow Control)
             * and the link partner advertised none, we will configure
             * ourselves to enable Rx Flow Control only.  We can do
             * this safely for two reasons:  If the link partner really
             * didn't want flow control enabled, and we enable Rx, no
             * harm done since we won't be receiving any PAUSE frames
             * anyway.  If the intent on the link partner was to have
             * flow control enabled, then by us enabling RX only, we
             * can at least receive pause frames and process them.
             * This is a good idea because in most cases, since we are
             * predominantly a server NIC, more times than not we will
             * be asked to delay transmission of packets than asking
             * our link partner to pause transmission of frames.
             */
            else if ((hw->original_fc == E1000_FC_NONE ||
                      hw->original_fc == E1000_FC_TX_PAUSE) ||
                      hw->fc_strict_ieee) {
                hw->fc = E1000_FC_NONE;
                DEBUGOUT("Flow Control = NONE.\n");
            } else {
                hw->fc = E1000_FC_RX_PAUSE;
                DEBUGOUT("Flow Control = RX PAUSE frames only.\n");
            }

            /* Now we need to do one last check...  If we auto-
             * negotiated to HALF DUPLEX, flow control should not be
             * enabled per IEEE 802.3 spec.
             */
            ret_val = e1000_get_speed_and_duplex(hw, &speed, &duplex);
            if (ret_val) {
                DEBUGOUT("Error getting link speed and duplex\n");
                return ret_val;
            }

            if (duplex == HALF_DUPLEX)
                hw->fc = E1000_FC_NONE;

            /* Now we call a subroutine to actually force the MAC
             * controller to use the correct flow control settings.
             */
            ret_val = e1000_force_mac_fc(hw);
            if (ret_val) {
                DEBUGOUT("Error forcing flow control settings\n");
                return ret_val;
            }
        } else {
            DEBUGOUT("Copper PHY and Auto Neg has not completed.\n");
        }
    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Checks to see if the link status of the hardware has changed.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Called by any function that needs to check the link status of the adapter.
 *****************************************************************************/
int32_t
e1000_check_for_link(struct e1000_hw *hw)
{
    uint32_t rxcw = 0;
    uint32_t ctrl;
    uint32_t status;
    uint32_t rctl;
    uint32_t icr;
    uint32_t signal = 0;
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_check_for_link");

    ctrl = E1000_READ_REG(hw, CTRL);
    status = E1000_READ_REG(hw, STATUS);

    /* On adapters with a MAC newer than 82544, SW Defineable pin 1 will be
     * set when the optics detect a signal. On older adapters, it will be
     * cleared when there is a signal.  This applies to fiber media only.
     */
    if ((hw->media_type == e1000_media_type_fiber) ||
        (hw->media_type == e1000_media_type_internal_serdes)) {
        rxcw = E1000_READ_REG(hw, RXCW);

        if (hw->media_type == e1000_media_type_fiber) {
            signal = (hw->mac_type > e1000_82544) ? E1000_CTRL_SWDPIN1 : 0;
            if (status & E1000_STATUS_LU)
                hw->get_link_status = FALSE;
        }
    }

    /* If we have a copper PHY then we only want to go out to the PHY
     * registers to see if Auto-Neg has completed and/or if our link
     * status has changed.  The get_link_status flag will be set if we
     * receive a Link Status Change interrupt or we have Rx Sequence
     * Errors.
     */
    if ((hw->media_type == e1000_media_type_copper) && hw->get_link_status) {
        /* First we want to see if the MII Status Register reports
         * link.  If so, then we want to get the current speed/duplex
         * of the PHY.
         * Read the register twice since the link bit is sticky.
         */
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;

        if (phy_data & MII_SR_LINK_STATUS) {
            hw->get_link_status = FALSE;
            /* Check if there was DownShift, must be checked immediately after
             * link-up */
            e1000_check_downshift(hw);

            /* If we are on 82544 or 82543 silicon and speed/duplex
             * are forced to 10H or 10F, then we will implement the polarity
             * reversal workaround.  We disable interrupts first, and upon
             * returning, place the devices interrupt state to its previous
             * value except for the link status change interrupt which will
             * happen due to the execution of this workaround.
             */

            if ((hw->mac_type == e1000_82544 || hw->mac_type == e1000_82543) &&
                (!hw->autoneg) &&
                (hw->forced_speed_duplex == e1000_10_full ||
                 hw->forced_speed_duplex == e1000_10_half)) {
                E1000_WRITE_REG(hw, IMC, 0xffffffff);
                ret_val = e1000_polarity_reversal_workaround(hw);
                icr = E1000_READ_REG(hw, ICR);
                E1000_WRITE_REG(hw, ICS, (icr & ~E1000_ICS_LSC));
                E1000_WRITE_REG(hw, IMS, IMS_ENABLE_MASK);
            }

        } else {
            /* No link detected */
            e1000_config_dsp_after_link_change(hw, FALSE);
            return 0;
        }

        /* If we are forcing speed/duplex, then we simply return since
         * we have already determined whether we have link or not.
         */
        if (!hw->autoneg) return -E1000_ERR_CONFIG;

        /* optimize the dsp settings for the igp phy */
        e1000_config_dsp_after_link_change(hw, TRUE);

        /* We have a M88E1000 PHY and Auto-Neg is enabled.  If we
         * have Si on board that is 82544 or newer, Auto
         * Speed Detection takes care of MAC speed/duplex
         * configuration.  So we only need to configure Collision
         * Distance in the MAC.  Otherwise, we need to force
         * speed/duplex on the MAC to the current PHY speed/duplex
         * settings.
         */
        if (hw->mac_type >= e1000_82544)
            e1000_config_collision_dist(hw);
        else {
            ret_val = e1000_config_mac_to_phy(hw);
            if (ret_val) {
                DEBUGOUT("Error configuring MAC to PHY settings\n");
                return ret_val;
            }
        }

        /* Configure Flow Control now that Auto-Neg has completed. First, we
         * need to restore the desired flow control settings because we may
         * have had to re-autoneg with a different link partner.
         */
        ret_val = e1000_config_fc_after_link_up(hw);
        if (ret_val) {
            DEBUGOUT("Error configuring flow control\n");
            return ret_val;
        }

        /* At this point we know that we are on copper and we have
         * auto-negotiated link.  These are conditions for checking the link
         * partner capability register.  We use the link speed to determine if
         * TBI compatibility needs to be turned on or off.  If the link is not
         * at gigabit speed, then TBI compatibility is not needed.  If we are
         * at gigabit speed, we turn on TBI compatibility.
         */
        if (hw->tbi_compatibility_en) {
            uint16_t speed, duplex;
            ret_val = e1000_get_speed_and_duplex(hw, &speed, &duplex);
            if (ret_val) {
                DEBUGOUT("Error getting link speed and duplex\n");
                return ret_val;
            }
            if (speed != SPEED_1000) {
                /* If link speed is not set to gigabit speed, we do not need
                 * to enable TBI compatibility.
                 */
                if (hw->tbi_compatibility_on) {
                    /* If we previously were in the mode, turn it off. */
                    rctl = E1000_READ_REG(hw, RCTL);
                    rctl &= ~E1000_RCTL_SBP;
                    E1000_WRITE_REG(hw, RCTL, rctl);
                    hw->tbi_compatibility_on = FALSE;
                }
            } else {
                /* If TBI compatibility is was previously off, turn it on. For
                 * compatibility with a TBI link partner, we will store bad
                 * packets. Some frames have an additional byte on the end and
                 * will look like CRC errors to to the hardware.
                 */
                if (!hw->tbi_compatibility_on) {
                    hw->tbi_compatibility_on = TRUE;
                    rctl = E1000_READ_REG(hw, RCTL);
                    rctl |= E1000_RCTL_SBP;
                    E1000_WRITE_REG(hw, RCTL, rctl);
                }
            }
        }
    }
    /* If we don't have link (auto-negotiation failed or link partner cannot
     * auto-negotiate), the cable is plugged in (we have signal), and our
     * link partner is not trying to auto-negotiate with us (we are receiving
     * idles or data), we need to force link up. We also need to give
     * auto-negotiation time to complete, in case the cable was just plugged
     * in. The autoneg_failed flag does this.
     */
    else if ((((hw->media_type == e1000_media_type_fiber) &&
              ((ctrl & E1000_CTRL_SWDPIN1) == signal)) ||
              (hw->media_type == e1000_media_type_internal_serdes)) &&
              (!(status & E1000_STATUS_LU)) &&
              (!(rxcw & E1000_RXCW_C))) {
        if (hw->autoneg_failed == 0) {
            hw->autoneg_failed = 1;
            return 0;
        }
        DEBUGOUT("NOT RXing /C/, disable AutoNeg and force link.\n");

        /* Disable auto-negotiation in the TXCW register */
        E1000_WRITE_REG(hw, TXCW, (hw->txcw & ~E1000_TXCW_ANE));

        /* Force link-up and also force full-duplex. */
        ctrl = E1000_READ_REG(hw, CTRL);
        ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
        E1000_WRITE_REG(hw, CTRL, ctrl);

        /* Configure Flow Control after forcing link up. */
        ret_val = e1000_config_fc_after_link_up(hw);
        if (ret_val) {
            DEBUGOUT("Error configuring flow control\n");
            return ret_val;
        }
    }
    /* If we are forcing link and we are receiving /C/ ordered sets, re-enable
     * auto-negotiation in the TXCW register and disable forced link in the
     * Device Control register in an attempt to auto-negotiate with our link
     * partner.
     */
    else if (((hw->media_type == e1000_media_type_fiber) ||
              (hw->media_type == e1000_media_type_internal_serdes)) &&
              (ctrl & E1000_CTRL_SLU) && (rxcw & E1000_RXCW_C)) {
        DEBUGOUT("RXing /C/, enable AutoNeg and stop forcing link.\n");
        E1000_WRITE_REG(hw, TXCW, hw->txcw);
        E1000_WRITE_REG(hw, CTRL, (ctrl & ~E1000_CTRL_SLU));

        hw->serdes_link_down = FALSE;
    }
    /* If we force link for non-auto-negotiation switch, check link status
     * based on MAC synchronization for internal serdes media type.
     */
    else if ((hw->media_type == e1000_media_type_internal_serdes) &&
             !(E1000_TXCW_ANE & E1000_READ_REG(hw, TXCW))) {
        /* SYNCH bit and IV bit are sticky. */
        udelay(10);
        if (E1000_RXCW_SYNCH & E1000_READ_REG(hw, RXCW)) {
            if (!(rxcw & E1000_RXCW_IV)) {
                hw->serdes_link_down = FALSE;
                DEBUGOUT("SERDES: Link is up.\n");
            }
        } else {
            hw->serdes_link_down = TRUE;
            DEBUGOUT("SERDES: Link is down.\n");
        }
    }
    if ((hw->media_type == e1000_media_type_internal_serdes) &&
        (E1000_TXCW_ANE & E1000_READ_REG(hw, TXCW))) {
        hw->serdes_link_down = !(E1000_STATUS_LU & E1000_READ_REG(hw, STATUS));
    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Detects the current speed and duplex settings of the hardware.
 *
 * hw - Struct containing variables accessed by shared code
 * speed - Speed of the connection
 * duplex - Duplex setting of the connection
 *****************************************************************************/
int32_t
e1000_get_speed_and_duplex(struct e1000_hw *hw,
                           uint16_t *speed,
                           uint16_t *duplex)
{
    uint32_t status;
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_get_speed_and_duplex");

    if (hw->mac_type >= e1000_82543) {
        status = E1000_READ_REG(hw, STATUS);
        if (status & E1000_STATUS_SPEED_1000) {
            *speed = SPEED_1000;
            DEBUGOUT("1000 Mbs, ");
        } else if (status & E1000_STATUS_SPEED_100) {
            *speed = SPEED_100;
            DEBUGOUT("100 Mbs, ");
        } else {
            *speed = SPEED_10;
            DEBUGOUT("10 Mbs, ");
        }

        if (status & E1000_STATUS_FD) {
            *duplex = FULL_DUPLEX;
            DEBUGOUT("Full Duplex\n");
        } else {
            *duplex = HALF_DUPLEX;
            DEBUGOUT(" Half Duplex\n");
        }
    } else {
        DEBUGOUT("1000 Mbs, Full Duplex\n");
        *speed = SPEED_1000;
        *duplex = FULL_DUPLEX;
    }

    /* IGP01 PHY may advertise full duplex operation after speed downgrade even
     * if it is operating at half duplex.  Here we set the duplex settings to
     * match the duplex in the link partner's capabilities.
     */
    if (hw->phy_type == e1000_phy_igp && hw->speed_downgraded) {
        ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_EXP, &phy_data);
        if (ret_val)
            return ret_val;

        if (!(phy_data & NWAY_ER_LP_NWAY_CAPS))
            *duplex = HALF_DUPLEX;
        else {
            ret_val = e1000_read_phy_reg(hw, PHY_LP_ABILITY, &phy_data);
            if (ret_val)
                return ret_val;
            if ((*speed == SPEED_100 && !(phy_data & NWAY_LPAR_100TX_FD_CAPS)) ||
               (*speed == SPEED_10 && !(phy_data & NWAY_LPAR_10T_FD_CAPS)))
                *duplex = HALF_DUPLEX;
        }
    }

    if ((hw->mac_type == e1000_80003es2lan) &&
        (hw->media_type == e1000_media_type_copper)) {
        if (*speed == SPEED_1000)
            ret_val = e1000_configure_kmrn_for_1000(hw);
        else
            ret_val = e1000_configure_kmrn_for_10_100(hw, *duplex);
        if (ret_val)
            return ret_val;
    }

    if ((hw->phy_type == e1000_phy_igp_3) && (*speed == SPEED_1000)) {
        ret_val = e1000_kumeran_lock_loss_workaround(hw);
        if (ret_val)
            return ret_val;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Blocks until autoneg completes or times out (~4.5 seconds)
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_wait_autoneg(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t i;
    uint16_t phy_data;

    DEBUGFUNC("e1000_wait_autoneg");
    DEBUGOUT("Waiting for Auto-Neg to complete.\n");

    /* We will wait for autoneg to complete or 4.5 seconds to expire. */
    for (i = PHY_AUTO_NEG_TIME; i > 0; i--) {
        /* Read the MII Status Register and wait for Auto-Neg
         * Complete bit to be set.
         */
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;
        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
        if (ret_val)
            return ret_val;
        if (phy_data & MII_SR_AUTONEG_COMPLETE) {
            return E1000_SUCCESS;
        }
        msleep(100);
    }
    return E1000_SUCCESS;
}

/******************************************************************************
* Raises the Management Data Clock
*
* hw - Struct containing variables accessed by shared code
* ctrl - Device control register's current value
******************************************************************************/
static void
e1000_raise_mdi_clk(struct e1000_hw *hw,
                    uint32_t *ctrl)
{
    /* Raise the clock input to the Management Data Clock (by setting the MDC
     * bit), and then delay 10 microseconds.
     */
    E1000_WRITE_REG(hw, CTRL, (*ctrl | E1000_CTRL_MDC));
    E1000_WRITE_FLUSH(hw);
    udelay(10);
}

/******************************************************************************
* Lowers the Management Data Clock
*
* hw - Struct containing variables accessed by shared code
* ctrl - Device control register's current value
******************************************************************************/
static void
e1000_lower_mdi_clk(struct e1000_hw *hw,
                    uint32_t *ctrl)
{
    /* Lower the clock input to the Management Data Clock (by clearing the MDC
     * bit), and then delay 10 microseconds.
     */
    E1000_WRITE_REG(hw, CTRL, (*ctrl & ~E1000_CTRL_MDC));
    E1000_WRITE_FLUSH(hw);
    udelay(10);
}

/******************************************************************************
* Shifts data bits out to the PHY
*
* hw - Struct containing variables accessed by shared code
* data - Data to send out to the PHY
* count - Number of bits to shift out
*
* Bits are shifted out in MSB to LSB order.
******************************************************************************/
static void
e1000_shift_out_mdi_bits(struct e1000_hw *hw,
                         uint32_t data,
                         uint16_t count)
{
    uint32_t ctrl;
    uint32_t mask;

    /* We need to shift "count" number of bits out to the PHY. So, the value
     * in the "data" parameter will be shifted out to the PHY one bit at a
     * time. In order to do this, "data" must be broken down into bits.
     */
    mask = 0x01;
    mask <<= (count - 1);

    ctrl = E1000_READ_REG(hw, CTRL);

    /* Set MDIO_DIR and MDC_DIR direction bits to be used as output pins. */
    ctrl |= (E1000_CTRL_MDIO_DIR | E1000_CTRL_MDC_DIR);

    while (mask) {
        /* A "1" is shifted out to the PHY by setting the MDIO bit to "1" and
         * then raising and lowering the Management Data Clock. A "0" is
         * shifted out to the PHY by setting the MDIO bit to "0" and then
         * raising and lowering the clock.
         */
        if (data & mask)
            ctrl |= E1000_CTRL_MDIO;
        else
            ctrl &= ~E1000_CTRL_MDIO;

        E1000_WRITE_REG(hw, CTRL, ctrl);
        E1000_WRITE_FLUSH(hw);

        udelay(10);

        e1000_raise_mdi_clk(hw, &ctrl);
        e1000_lower_mdi_clk(hw, &ctrl);

        mask = mask >> 1;
    }
}

/******************************************************************************
* Shifts data bits in from the PHY
*
* hw - Struct containing variables accessed by shared code
*
* Bits are shifted in in MSB to LSB order.
******************************************************************************/
static uint16_t
e1000_shift_in_mdi_bits(struct e1000_hw *hw)
{
    uint32_t ctrl;
    uint16_t data = 0;
    uint8_t i;

    /* In order to read a register from the PHY, we need to shift in a total
     * of 18 bits from the PHY. The first two bit (turnaround) times are used
     * to avoid contention on the MDIO pin when a read operation is performed.
     * These two bits are ignored by us and thrown away. Bits are "shifted in"
     * by raising the input to the Management Data Clock (setting the MDC bit),
     * and then reading the value of the MDIO bit.
     */
    ctrl = E1000_READ_REG(hw, CTRL);

    /* Clear MDIO_DIR (SWDPIO1) to indicate this bit is to be used as input. */
    ctrl &= ~E1000_CTRL_MDIO_DIR;
    ctrl &= ~E1000_CTRL_MDIO;

    E1000_WRITE_REG(hw, CTRL, ctrl);
    E1000_WRITE_FLUSH(hw);

    /* Raise and Lower the clock before reading in the data. This accounts for
     * the turnaround bits. The first clock occurred when we clocked out the
     * last bit of the Register Address.
     */
    e1000_raise_mdi_clk(hw, &ctrl);
    e1000_lower_mdi_clk(hw, &ctrl);

    for (data = 0, i = 0; i < 16; i++) {
        data = data << 1;
        e1000_raise_mdi_clk(hw, &ctrl);
        ctrl = E1000_READ_REG(hw, CTRL);
        /* Check to see if we shifted in a "1". */
        if (ctrl & E1000_CTRL_MDIO)
            data |= 1;
        e1000_lower_mdi_clk(hw, &ctrl);
    }

    e1000_raise_mdi_clk(hw, &ctrl);
    e1000_lower_mdi_clk(hw, &ctrl);

    return data;
}

static int32_t
e1000_swfw_sync_acquire(struct e1000_hw *hw, uint16_t mask)
{
    uint32_t swfw_sync = 0;
    uint32_t swmask = mask;
    uint32_t fwmask = mask << 16;
    int32_t timeout = 200;

    DEBUGFUNC("e1000_swfw_sync_acquire");

    if (hw->swfwhw_semaphore_present)
        return e1000_get_software_flag(hw);

    if (!hw->swfw_sync_present)
        return e1000_get_hw_eeprom_semaphore(hw);

    while (timeout) {
            if (e1000_get_hw_eeprom_semaphore(hw))
                return -E1000_ERR_SWFW_SYNC;

            swfw_sync = E1000_READ_REG(hw, SW_FW_SYNC);
            if (!(swfw_sync & (fwmask | swmask))) {
                break;
            }

            /* firmware currently using resource (fwmask) */
            /* or other software thread currently using resource (swmask) */
            e1000_put_hw_eeprom_semaphore(hw);
            mdelay(5);
            timeout--;
    }

    if (!timeout) {
        DEBUGOUT("Driver can't access resource, SW_FW_SYNC timeout.\n");
        return -E1000_ERR_SWFW_SYNC;
    }

    swfw_sync |= swmask;
    E1000_WRITE_REG(hw, SW_FW_SYNC, swfw_sync);

    e1000_put_hw_eeprom_semaphore(hw);
    return E1000_SUCCESS;
}

static void
e1000_swfw_sync_release(struct e1000_hw *hw, uint16_t mask)
{
    uint32_t swfw_sync;
    uint32_t swmask = mask;

    DEBUGFUNC("e1000_swfw_sync_release");

    if (hw->swfwhw_semaphore_present) {
        e1000_release_software_flag(hw);
        return;
    }

    if (!hw->swfw_sync_present) {
        e1000_put_hw_eeprom_semaphore(hw);
        return;
    }

    /* if (e1000_get_hw_eeprom_semaphore(hw))
     *    return -E1000_ERR_SWFW_SYNC; */
    while (e1000_get_hw_eeprom_semaphore(hw) != E1000_SUCCESS);
        /* empty */

    swfw_sync = E1000_READ_REG(hw, SW_FW_SYNC);
    swfw_sync &= ~swmask;
    E1000_WRITE_REG(hw, SW_FW_SYNC, swfw_sync);

    e1000_put_hw_eeprom_semaphore(hw);
}

/*****************************************************************************
* Reads the value from a PHY register, if the value is on a specific non zero
* page, sets the page first.
* hw - Struct containing variables accessed by shared code
* reg_addr - address of the PHY register to read
******************************************************************************/
int32_t
e1000_read_phy_reg(struct e1000_hw *hw,
                   uint32_t reg_addr,
                   uint16_t *phy_data)
{
    uint32_t ret_val;
    uint16_t swfw;

    DEBUGFUNC("e1000_read_phy_reg");

    if ((hw->mac_type == e1000_80003es2lan) &&
        (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
        swfw = E1000_SWFW_PHY1_SM;
    } else {
        swfw = E1000_SWFW_PHY0_SM;
    }
    if (e1000_swfw_sync_acquire(hw, swfw))
        return -E1000_ERR_SWFW_SYNC;

    if ((hw->phy_type == e1000_phy_igp ||
        hw->phy_type == e1000_phy_igp_3 ||
        hw->phy_type == e1000_phy_igp_2) &&
       (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
        ret_val = e1000_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
                                         (uint16_t)reg_addr);
        if (ret_val) {
            e1000_swfw_sync_release(hw, swfw);
            return ret_val;
        }
    } else if (hw->phy_type == e1000_phy_gg82563) {
        if (((reg_addr & MAX_PHY_REG_ADDRESS) > MAX_PHY_MULTI_PAGE_REG) ||
            (hw->mac_type == e1000_80003es2lan)) {
            /* Select Configuration Page */
            if ((reg_addr & MAX_PHY_REG_ADDRESS) < GG82563_MIN_ALT_REG) {
                ret_val = e1000_write_phy_reg_ex(hw, GG82563_PHY_PAGE_SELECT,
                          (uint16_t)((uint16_t)reg_addr >> GG82563_PAGE_SHIFT));
            } else {
                /* Use Alternative Page Select register to access
                 * registers 30 and 31
                 */
                ret_val = e1000_write_phy_reg_ex(hw,
                                                 GG82563_PHY_PAGE_SELECT_ALT,
                          (uint16_t)((uint16_t)reg_addr >> GG82563_PAGE_SHIFT));
            }

            if (ret_val) {
                e1000_swfw_sync_release(hw, swfw);
                return ret_val;
            }
        }
    }

    ret_val = e1000_read_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
                                    phy_data);

    e1000_swfw_sync_release(hw, swfw);
    return ret_val;
}

static int32_t
e1000_read_phy_reg_ex(struct e1000_hw *hw, uint32_t reg_addr,
                      uint16_t *phy_data)
{
    uint32_t i;
    uint32_t mdic = 0;
    const uint32_t phy_addr = 1;

    DEBUGFUNC("e1000_read_phy_reg_ex");

    if (reg_addr > MAX_PHY_REG_ADDRESS) {
        DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
        return -E1000_ERR_PARAM;
    }

    if (hw->mac_type > e1000_82543) {
        /* Set up Op-code, Phy Address, and register address in the MDI
         * Control register.  The MAC will take care of interfacing with the
         * PHY to retrieve the desired data.
         */
        mdic = ((reg_addr << E1000_MDIC_REG_SHIFT) |
                (phy_addr << E1000_MDIC_PHY_SHIFT) |
                (E1000_MDIC_OP_READ));

        E1000_WRITE_REG(hw, MDIC, mdic);

        /* Poll the ready bit to see if the MDI read completed */
        for (i = 0; i < 64; i++) {
            udelay(50);
            mdic = E1000_READ_REG(hw, MDIC);
            if (mdic & E1000_MDIC_READY) break;
        }
        if (!(mdic & E1000_MDIC_READY)) {
            DEBUGOUT("MDI Read did not complete\n");
            return -E1000_ERR_PHY;
        }
        if (mdic & E1000_MDIC_ERROR) {
            DEBUGOUT("MDI Error\n");
            return -E1000_ERR_PHY;
        }
        *phy_data = (uint16_t) mdic;
    } else {
        /* We must first send a preamble through the MDIO pin to signal the
         * beginning of an MII instruction.  This is done by sending 32
         * consecutive "1" bits.
         */
        e1000_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

        /* Now combine the next few fields that are required for a read
         * operation.  We use this method instead of calling the
         * e1000_shift_out_mdi_bits routine five different times. The format of
         * a MII read instruction consists of a shift out of 14 bits and is
         * defined as follows:
         *    <Preamble><SOF><Op Code><Phy Addr><Reg Addr>
         * followed by a shift in of 18 bits.  This first two bits shifted in
         * are TurnAround bits used to avoid contention on the MDIO pin when a
         * READ operation is performed.  These two bits are thrown away
         * followed by a shift in of 16 bits which contains the desired data.
         */
        mdic = ((reg_addr) | (phy_addr << 5) |
                (PHY_OP_READ << 10) | (PHY_SOF << 12));

        e1000_shift_out_mdi_bits(hw, mdic, 14);

        /* Now that we've shifted out the read command to the MII, we need to
         * "shift in" the 16-bit value (18 total bits) of the requested PHY
         * register address.
         */
        *phy_data = e1000_shift_in_mdi_bits(hw);
    }
    return E1000_SUCCESS;
}

/******************************************************************************
* Writes a value to a PHY register
*
* hw - Struct containing variables accessed by shared code
* reg_addr - address of the PHY register to write
* data - data to write to the PHY
******************************************************************************/
int32_t
e1000_write_phy_reg(struct e1000_hw *hw, uint32_t reg_addr,
                    uint16_t phy_data)
{
    uint32_t ret_val;
    uint16_t swfw;

    DEBUGFUNC("e1000_write_phy_reg");

    if ((hw->mac_type == e1000_80003es2lan) &&
        (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
        swfw = E1000_SWFW_PHY1_SM;
    } else {
        swfw = E1000_SWFW_PHY0_SM;
    }
    if (e1000_swfw_sync_acquire(hw, swfw))
        return -E1000_ERR_SWFW_SYNC;

    if ((hw->phy_type == e1000_phy_igp ||
        hw->phy_type == e1000_phy_igp_3 ||
        hw->phy_type == e1000_phy_igp_2) &&
       (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
        ret_val = e1000_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
                                         (uint16_t)reg_addr);
        if (ret_val) {
            e1000_swfw_sync_release(hw, swfw);
            return ret_val;
        }
    } else if (hw->phy_type == e1000_phy_gg82563) {
        if (((reg_addr & MAX_PHY_REG_ADDRESS) > MAX_PHY_MULTI_PAGE_REG) ||
            (hw->mac_type == e1000_80003es2lan)) {
            /* Select Configuration Page */
            if ((reg_addr & MAX_PHY_REG_ADDRESS) < GG82563_MIN_ALT_REG) {
                ret_val = e1000_write_phy_reg_ex(hw, GG82563_PHY_PAGE_SELECT,
                          (uint16_t)((uint16_t)reg_addr >> GG82563_PAGE_SHIFT));
            } else {
                /* Use Alternative Page Select register to access
                 * registers 30 and 31
                 */
                ret_val = e1000_write_phy_reg_ex(hw,
                                                 GG82563_PHY_PAGE_SELECT_ALT,
                          (uint16_t)((uint16_t)reg_addr >> GG82563_PAGE_SHIFT));
            }

            if (ret_val) {
                e1000_swfw_sync_release(hw, swfw);
                return ret_val;
            }
        }
    }

    ret_val = e1000_write_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
                                     phy_data);

    e1000_swfw_sync_release(hw, swfw);
    return ret_val;
}

static int32_t
e1000_write_phy_reg_ex(struct e1000_hw *hw, uint32_t reg_addr,
                       uint16_t phy_data)
{
    uint32_t i;
    uint32_t mdic = 0;
    const uint32_t phy_addr = 1;

    DEBUGFUNC("e1000_write_phy_reg_ex");

    if (reg_addr > MAX_PHY_REG_ADDRESS) {
        DEBUGOUT1("PHY Address %d is out of range\n", reg_addr);
        return -E1000_ERR_PARAM;
    }

    if (hw->mac_type > e1000_82543) {
        /* Set up Op-code, Phy Address, register address, and data intended
         * for the PHY register in the MDI Control register.  The MAC will take
         * care of interfacing with the PHY to send the desired data.
         */
        mdic = (((uint32_t) phy_data) |
                (reg_addr << E1000_MDIC_REG_SHIFT) |
                (phy_addr << E1000_MDIC_PHY_SHIFT) |
                (E1000_MDIC_OP_WRITE));

        E1000_WRITE_REG(hw, MDIC, mdic);

        /* Poll the ready bit to see if the MDI read completed */
        for (i = 0; i < 641; i++) {
            udelay(5);
            mdic = E1000_READ_REG(hw, MDIC);
            if (mdic & E1000_MDIC_READY) break;
        }
        if (!(mdic & E1000_MDIC_READY)) {
            DEBUGOUT("MDI Write did not complete\n");
            return -E1000_ERR_PHY;
        }
    } else {
        /* We'll need to use the SW defined pins to shift the write command
         * out to the PHY. We first send a preamble to the PHY to signal the
         * beginning of the MII instruction.  This is done by sending 32
         * consecutive "1" bits.
         */
        e1000_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

        /* Now combine the remaining required fields that will indicate a
         * write operation. We use this method instead of calling the
         * e1000_shift_out_mdi_bits routine for each field in the command. The
         * format of a MII write instruction is as follows:
         * <Preamble><SOF><Op Code><Phy Addr><Reg Addr><Turnaround><Data>.
         */
        mdic = ((PHY_TURNAROUND) | (reg_addr << 2) | (phy_addr << 7) |
                (PHY_OP_WRITE << 12) | (PHY_SOF << 14));
        mdic <<= 16;
        mdic |= (uint32_t) phy_data;

        e1000_shift_out_mdi_bits(hw, mdic, 32);
    }

    return E1000_SUCCESS;
}

static int32_t
e1000_read_kmrn_reg(struct e1000_hw *hw,
                    uint32_t reg_addr,
                    uint16_t *data)
{
    uint32_t reg_val;
    uint16_t swfw;
    DEBUGFUNC("e1000_read_kmrn_reg");

    if ((hw->mac_type == e1000_80003es2lan) &&
        (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
        swfw = E1000_SWFW_PHY1_SM;
    } else {
        swfw = E1000_SWFW_PHY0_SM;
    }
    if (e1000_swfw_sync_acquire(hw, swfw))
        return -E1000_ERR_SWFW_SYNC;

    /* Write register address */
    reg_val = ((reg_addr << E1000_KUMCTRLSTA_OFFSET_SHIFT) &
              E1000_KUMCTRLSTA_OFFSET) |
              E1000_KUMCTRLSTA_REN;
    E1000_WRITE_REG(hw, KUMCTRLSTA, reg_val);
    udelay(2);

    /* Read the data returned */
    reg_val = E1000_READ_REG(hw, KUMCTRLSTA);
    *data = (uint16_t)reg_val;

    e1000_swfw_sync_release(hw, swfw);
    return E1000_SUCCESS;
}

static int32_t
e1000_write_kmrn_reg(struct e1000_hw *hw,
                     uint32_t reg_addr,
                     uint16_t data)
{
    uint32_t reg_val;
    uint16_t swfw;
    DEBUGFUNC("e1000_write_kmrn_reg");

    if ((hw->mac_type == e1000_80003es2lan) &&
        (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
        swfw = E1000_SWFW_PHY1_SM;
    } else {
        swfw = E1000_SWFW_PHY0_SM;
    }
    if (e1000_swfw_sync_acquire(hw, swfw))
        return -E1000_ERR_SWFW_SYNC;

    reg_val = ((reg_addr << E1000_KUMCTRLSTA_OFFSET_SHIFT) &
              E1000_KUMCTRLSTA_OFFSET) | data;
    E1000_WRITE_REG(hw, KUMCTRLSTA, reg_val);
    udelay(2);

    e1000_swfw_sync_release(hw, swfw);
    return E1000_SUCCESS;
}

/******************************************************************************
* Returns the PHY to the power-on reset state
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
int32_t
e1000_phy_hw_reset(struct e1000_hw *hw)
{
    uint32_t ctrl, ctrl_ext;
    uint32_t led_ctrl;
    int32_t ret_val;
    uint16_t swfw;

    DEBUGFUNC("e1000_phy_hw_reset");

    /* In the case of the phy reset being blocked, it's not an error, we
     * simply return success without performing the reset. */
    ret_val = e1000_check_phy_reset_block(hw);
    if (ret_val)
        return E1000_SUCCESS;

    DEBUGOUT("Resetting Phy...\n");

    if (hw->mac_type > e1000_82543) {
        if ((hw->mac_type == e1000_80003es2lan) &&
            (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)) {
            swfw = E1000_SWFW_PHY1_SM;
        } else {
            swfw = E1000_SWFW_PHY0_SM;
        }
        if (e1000_swfw_sync_acquire(hw, swfw)) {
            DEBUGOUT("Unable to acquire swfw sync\n");
            return -E1000_ERR_SWFW_SYNC;
        }
        /* Read the device control register and assert the E1000_CTRL_PHY_RST
         * bit. Then, take it out of reset.
         * For pre-e1000_82571 hardware, we delay for 10ms between the assert
         * and deassert.  For e1000_82571 hardware and later, we instead delay
         * for 50us between and 10ms after the deassertion.
         */
        ctrl = E1000_READ_REG(hw, CTRL);
        E1000_WRITE_REG(hw, CTRL, ctrl | E1000_CTRL_PHY_RST);
        E1000_WRITE_FLUSH(hw);

        if (hw->mac_type < e1000_82571)
            msleep(10);
        else
            udelay(100);

        E1000_WRITE_REG(hw, CTRL, ctrl);
        E1000_WRITE_FLUSH(hw);

        if (hw->mac_type >= e1000_82571)
            mdelay(10);

        e1000_swfw_sync_release(hw, swfw);
    } else {
        /* Read the Extended Device Control Register, assert the PHY_RESET_DIR
         * bit to put the PHY into reset. Then, take it out of reset.
         */
        ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
        ctrl_ext |= E1000_CTRL_EXT_SDP4_DIR;
        ctrl_ext &= ~E1000_CTRL_EXT_SDP4_DATA;
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
        E1000_WRITE_FLUSH(hw);
        msleep(10);
        ctrl_ext |= E1000_CTRL_EXT_SDP4_DATA;
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
        E1000_WRITE_FLUSH(hw);
    }
    udelay(150);

    if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
        /* Configure activity LED after PHY reset */
        led_ctrl = E1000_READ_REG(hw, LEDCTL);
        led_ctrl &= IGP_ACTIVITY_LED_MASK;
        led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
        E1000_WRITE_REG(hw, LEDCTL, led_ctrl);
    }

    /* Wait for FW to finish PHY configuration. */
    ret_val = e1000_get_phy_cfg_done(hw);
    if (ret_val != E1000_SUCCESS)
        return ret_val;
    e1000_release_software_semaphore(hw);

    if ((hw->mac_type == e1000_ich8lan) && (hw->phy_type == e1000_phy_igp_3))
        ret_val = e1000_init_lcd_from_nvm(hw);

    return ret_val;
}

/******************************************************************************
* Resets the PHY
*
* hw - Struct containing variables accessed by shared code
*
* Sets bit 15 of the MII Control register
******************************************************************************/
int32_t
e1000_phy_reset(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_phy_reset");

    /* In the case of the phy reset being blocked, it's not an error, we
     * simply return success without performing the reset. */
    ret_val = e1000_check_phy_reset_block(hw);
    if (ret_val)
        return E1000_SUCCESS;

    switch (hw->phy_type) {
    case e1000_phy_igp:
    case e1000_phy_igp_2:
    case e1000_phy_igp_3:
    case e1000_phy_ife:
        ret_val = e1000_phy_hw_reset(hw);
        if (ret_val)
            return ret_val;
        break;
    default:
        ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data |= MII_CR_RESET;
        ret_val = e1000_write_phy_reg(hw, PHY_CTRL, phy_data);
        if (ret_val)
            return ret_val;

        udelay(1);
        break;
    }

    if (hw->phy_type == e1000_phy_igp || hw->phy_type == e1000_phy_igp_2)
        e1000_phy_init_script(hw);

    return E1000_SUCCESS;
}

/******************************************************************************
* Work-around for 82566 power-down: on D3 entry-
* 1) disable gigabit link
* 2) write VR power-down enable
* 3) read it back
* if successful continue, else issue LCD reset and repeat
*
* hw - struct containing variables accessed by shared code
******************************************************************************/
void
e1000_phy_powerdown_workaround(struct e1000_hw *hw)
{
    int32_t reg;
    uint16_t phy_data;
    int32_t retry = 0;

    DEBUGFUNC("e1000_phy_powerdown_workaround");

    if (hw->phy_type != e1000_phy_igp_3)
        return;

    do {
        /* Disable link */
        reg = E1000_READ_REG(hw, PHY_CTRL);
        E1000_WRITE_REG(hw, PHY_CTRL, reg | E1000_PHY_CTRL_GBE_DISABLE |
                        E1000_PHY_CTRL_NOND0A_GBE_DISABLE);

        /* Write VR power-down enable - bits 9:8 should be 10b */
        e1000_read_phy_reg(hw, IGP3_VR_CTRL, &phy_data);
        phy_data |= (1 << 9);
        phy_data &= ~(1 << 8);
        e1000_write_phy_reg(hw, IGP3_VR_CTRL, phy_data);

        /* Read it back and test */
        e1000_read_phy_reg(hw, IGP3_VR_CTRL, &phy_data);
        if (((phy_data & IGP3_VR_CTRL_MODE_MASK) == IGP3_VR_CTRL_MODE_SHUT) || retry)
            break;

        /* Issue PHY reset and repeat at most one more time */
        reg = E1000_READ_REG(hw, CTRL);
        E1000_WRITE_REG(hw, CTRL, reg | E1000_CTRL_PHY_RST);
        retry++;
    } while (retry);

    return;

}

/******************************************************************************
* Work-around for 82566 Kumeran PCS lock loss:
* On link status change (i.e. PCI reset, speed change) and link is up and
* speed is gigabit-
* 0) if workaround is optionally disabled do nothing
* 1) wait 1ms for Kumeran link to come up
* 2) check Kumeran Diagnostic register PCS lock loss bit
* 3) if not set the link is locked (all is good), otherwise...
* 4) reset the PHY
* 5) repeat up to 10 times
* Note: this is only called for IGP3 copper when speed is 1gb.
*
* hw - struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_kumeran_lock_loss_workaround(struct e1000_hw *hw)
{
    int32_t ret_val;
    int32_t reg;
    int32_t cnt;
    uint16_t phy_data;

    if (hw->kmrn_lock_loss_workaround_disabled)
        return E1000_SUCCESS;

    /* Make sure link is up before proceeding.  If not just return.
     * Attempting this while link is negotiating fouled up link
     * stability */
    ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
    ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);

    if (phy_data & MII_SR_LINK_STATUS) {
        for (cnt = 0; cnt < 10; cnt++) {
            /* read once to clear */
            ret_val = e1000_read_phy_reg(hw, IGP3_KMRN_DIAG, &phy_data);
            if (ret_val)
                return ret_val;
            /* and again to get new status */
            ret_val = e1000_read_phy_reg(hw, IGP3_KMRN_DIAG, &phy_data);
            if (ret_val)
                return ret_val;

            /* check for PCS lock */
            if (!(phy_data & IGP3_KMRN_DIAG_PCS_LOCK_LOSS))
                return E1000_SUCCESS;

            /* Issue PHY reset */
            e1000_phy_hw_reset(hw);
            mdelay(5);
        }
        /* Disable GigE link negotiation */
        reg = E1000_READ_REG(hw, PHY_CTRL);
        E1000_WRITE_REG(hw, PHY_CTRL, reg | E1000_PHY_CTRL_GBE_DISABLE |
                        E1000_PHY_CTRL_NOND0A_GBE_DISABLE);

        /* unable to acquire PCS lock */
        return E1000_ERR_PHY;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Probes the expected PHY address for known PHY IDs
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_detect_gig_phy(struct e1000_hw *hw)
{
    int32_t phy_init_status, ret_val;
    uint16_t phy_id_high, phy_id_low;
    boolean_t match = FALSE;

    DEBUGFUNC("e1000_detect_gig_phy");

    if (hw->phy_id != 0)
        return E1000_SUCCESS;

    /* The 82571 firmware may still be configuring the PHY.  In this
     * case, we cannot access the PHY until the configuration is done.  So
     * we explicitly set the PHY values. */
    if (hw->mac_type == e1000_82571 ||
        hw->mac_type == e1000_82572) {
        hw->phy_id = IGP01E1000_I_PHY_ID;
        hw->phy_type = e1000_phy_igp_2;
        return E1000_SUCCESS;
    }

    /* ESB-2 PHY reads require e1000_phy_gg82563 to be set because of a work-
     * around that forces PHY page 0 to be set or the reads fail.  The rest of
     * the code in this routine uses e1000_read_phy_reg to read the PHY ID.
     * So for ESB-2 we need to have this set so our reads won't fail.  If the
     * attached PHY is not a e1000_phy_gg82563, the routines below will figure
     * this out as well. */
    if (hw->mac_type == e1000_80003es2lan)
        hw->phy_type = e1000_phy_gg82563;

    /* Read the PHY ID Registers to identify which PHY is onboard. */
    ret_val = e1000_read_phy_reg(hw, PHY_ID1, &phy_id_high);
    if (ret_val)
        return ret_val;

    hw->phy_id = (uint32_t) (phy_id_high << 16);
    udelay(20);
    ret_val = e1000_read_phy_reg(hw, PHY_ID2, &phy_id_low);
    if (ret_val)
        return ret_val;

    hw->phy_id |= (uint32_t) (phy_id_low & PHY_REVISION_MASK);
    hw->phy_revision = (uint32_t) phy_id_low & ~PHY_REVISION_MASK;

    switch (hw->mac_type) {
    case e1000_82543:
        if (hw->phy_id == M88E1000_E_PHY_ID) match = TRUE;
        break;
    case e1000_82544:
        if (hw->phy_id == M88E1000_I_PHY_ID) match = TRUE;
        break;
    case e1000_82540:
    case e1000_82545:
    case e1000_82545_rev_3:
    case e1000_82546:
    case e1000_82546_rev_3:
        if (hw->phy_id == M88E1011_I_PHY_ID) match = TRUE;
        break;
    case e1000_82541:
    case e1000_82541_rev_2:
    case e1000_82547:
    case e1000_82547_rev_2:
        if (hw->phy_id == IGP01E1000_I_PHY_ID) match = TRUE;
        break;
    case e1000_82573:
        if (hw->phy_id == M88E1111_I_PHY_ID) match = TRUE;
        break;
    case e1000_80003es2lan:
        if (hw->phy_id == GG82563_E_PHY_ID) match = TRUE;
        break;
    case e1000_ich8lan:
        if (hw->phy_id == IGP03E1000_E_PHY_ID) match = TRUE;
        if (hw->phy_id == IFE_E_PHY_ID) match = TRUE;
        if (hw->phy_id == IFE_PLUS_E_PHY_ID) match = TRUE;
        if (hw->phy_id == IFE_C_E_PHY_ID) match = TRUE;
        break;
    default:
        DEBUGOUT1("Invalid MAC type %d\n", hw->mac_type);
        return -E1000_ERR_CONFIG;
    }
    phy_init_status = e1000_set_phy_type(hw);

    if ((match) && (phy_init_status == E1000_SUCCESS)) {
        DEBUGOUT1("PHY ID 0x%X detected\n", hw->phy_id);
        return E1000_SUCCESS;
    }
    DEBUGOUT1("Invalid PHY ID 0x%X\n", hw->phy_id);
    return -E1000_ERR_PHY;
}

/******************************************************************************
* Resets the PHY's DSP
*
* hw - Struct containing variables accessed by shared code
******************************************************************************/
static int32_t
e1000_phy_reset_dsp(struct e1000_hw *hw)
{
    int32_t ret_val;
    DEBUGFUNC("e1000_phy_reset_dsp");

    do {
        if (hw->phy_type != e1000_phy_gg82563) {
            ret_val = e1000_write_phy_reg(hw, 29, 0x001d);
            if (ret_val) break;
        }
        ret_val = e1000_write_phy_reg(hw, 30, 0x00c1);
        if (ret_val) break;
        ret_val = e1000_write_phy_reg(hw, 30, 0x0000);
        if (ret_val) break;
        ret_val = E1000_SUCCESS;
    } while (0);

    return ret_val;
}

/******************************************************************************
* Get PHY information from various PHY registers for igp PHY only.
*
* hw - Struct containing variables accessed by shared code
* phy_info - PHY information structure
******************************************************************************/
static int32_t
e1000_phy_igp_get_info(struct e1000_hw *hw,
                       struct e1000_phy_info *phy_info)
{
    int32_t ret_val;
    uint16_t phy_data, min_length, max_length, average;
    e1000_rev_polarity polarity;

    DEBUGFUNC("e1000_phy_igp_get_info");

    /* The downshift status is checked only once, after link is established,
     * and it stored in the hw->speed_downgraded parameter. */
    phy_info->downshift = (e1000_downshift)hw->speed_downgraded;

    /* IGP01E1000 does not need to support it. */
    phy_info->extended_10bt_distance = e1000_10bt_ext_dist_enable_normal;

    /* IGP01E1000 always correct polarity reversal */
    phy_info->polarity_correction = e1000_polarity_reversal_enabled;

    /* Check polarity status */
    ret_val = e1000_check_polarity(hw, &polarity);
    if (ret_val)
        return ret_val;

    phy_info->cable_polarity = polarity;

    ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_STATUS, &phy_data);
    if (ret_val)
        return ret_val;

    phy_info->mdix_mode = (e1000_auto_x_mode)((phy_data & IGP01E1000_PSSR_MDIX) >>
                          IGP01E1000_PSSR_MDIX_SHIFT);

    if ((phy_data & IGP01E1000_PSSR_SPEED_MASK) ==
       IGP01E1000_PSSR_SPEED_1000MBPS) {
        /* Local/Remote Receiver Information are only valid at 1000 Mbps */
        ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_data);
        if (ret_val)
            return ret_val;

        phy_info->local_rx = ((phy_data & SR_1000T_LOCAL_RX_STATUS) >>
                             SR_1000T_LOCAL_RX_STATUS_SHIFT) ?
                             e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;
        phy_info->remote_rx = ((phy_data & SR_1000T_REMOTE_RX_STATUS) >>
                              SR_1000T_REMOTE_RX_STATUS_SHIFT) ?
                              e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;

        /* Get cable length */
        ret_val = e1000_get_cable_length(hw, &min_length, &max_length);
        if (ret_val)
            return ret_val;

        /* Translate to old method */
        average = (max_length + min_length) / 2;

        if (average <= e1000_igp_cable_length_50)
            phy_info->cable_length = e1000_cable_length_50;
        else if (average <= e1000_igp_cable_length_80)
            phy_info->cable_length = e1000_cable_length_50_80;
        else if (average <= e1000_igp_cable_length_110)
            phy_info->cable_length = e1000_cable_length_80_110;
        else if (average <= e1000_igp_cable_length_140)
            phy_info->cable_length = e1000_cable_length_110_140;
        else
            phy_info->cable_length = e1000_cable_length_140;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Get PHY information from various PHY registers for ife PHY only.
*
* hw - Struct containing variables accessed by shared code
* phy_info - PHY information structure
******************************************************************************/
static int32_t
e1000_phy_ife_get_info(struct e1000_hw *hw,
                       struct e1000_phy_info *phy_info)
{
    int32_t ret_val;
    uint16_t phy_data;
    e1000_rev_polarity polarity;

    DEBUGFUNC("e1000_phy_ife_get_info");

    phy_info->downshift = (e1000_downshift)hw->speed_downgraded;
    phy_info->extended_10bt_distance = e1000_10bt_ext_dist_enable_normal;

    ret_val = e1000_read_phy_reg(hw, IFE_PHY_SPECIAL_CONTROL, &phy_data);
    if (ret_val)
        return ret_val;
    phy_info->polarity_correction =
                        ((phy_data & IFE_PSC_AUTO_POLARITY_DISABLE) >>
                        IFE_PSC_AUTO_POLARITY_DISABLE_SHIFT) ?
                        e1000_polarity_reversal_disabled : e1000_polarity_reversal_enabled;

    if (phy_info->polarity_correction == e1000_polarity_reversal_enabled) {
        ret_val = e1000_check_polarity(hw, &polarity);
        if (ret_val)
            return ret_val;
    } else {
        /* Polarity is forced. */
        polarity = ((phy_data & IFE_PSC_FORCE_POLARITY) >>
                     IFE_PSC_FORCE_POLARITY_SHIFT) ?
                     e1000_rev_polarity_reversed : e1000_rev_polarity_normal;
    }
    phy_info->cable_polarity = polarity;

    ret_val = e1000_read_phy_reg(hw, IFE_PHY_MDIX_CONTROL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_info->mdix_mode = (e1000_auto_x_mode)
                     ((phy_data & (IFE_PMC_AUTO_MDIX | IFE_PMC_FORCE_MDIX)) >>
                     IFE_PMC_MDIX_MODE_SHIFT);

    return E1000_SUCCESS;
}

/******************************************************************************
* Get PHY information from various PHY registers fot m88 PHY only.
*
* hw - Struct containing variables accessed by shared code
* phy_info - PHY information structure
******************************************************************************/
static int32_t
e1000_phy_m88_get_info(struct e1000_hw *hw,
                       struct e1000_phy_info *phy_info)
{
    int32_t ret_val;
    uint16_t phy_data;
    e1000_rev_polarity polarity;

    DEBUGFUNC("e1000_phy_m88_get_info");

    /* The downshift status is checked only once, after link is established,
     * and it stored in the hw->speed_downgraded parameter. */
    phy_info->downshift = (e1000_downshift)hw->speed_downgraded;

    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_info->extended_10bt_distance =
        ((phy_data & M88E1000_PSCR_10BT_EXT_DIST_ENABLE) >>
        M88E1000_PSCR_10BT_EXT_DIST_ENABLE_SHIFT) ?
        e1000_10bt_ext_dist_enable_lower : e1000_10bt_ext_dist_enable_normal;

    phy_info->polarity_correction =
        ((phy_data & M88E1000_PSCR_POLARITY_REVERSAL) >>
        M88E1000_PSCR_POLARITY_REVERSAL_SHIFT) ?
        e1000_polarity_reversal_disabled : e1000_polarity_reversal_enabled;

    /* Check polarity status */
    ret_val = e1000_check_polarity(hw, &polarity);
    if (ret_val)
        return ret_val;
    phy_info->cable_polarity = polarity;

    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
    if (ret_val)
        return ret_val;

    phy_info->mdix_mode = (e1000_auto_x_mode)((phy_data & M88E1000_PSSR_MDIX) >>
                          M88E1000_PSSR_MDIX_SHIFT);

    if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS) {
        /* Cable Length Estimation and Local/Remote Receiver Information
         * are only valid at 1000 Mbps.
         */
        if (hw->phy_type != e1000_phy_gg82563) {
            phy_info->cable_length = (e1000_cable_length)((phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
                                      M88E1000_PSSR_CABLE_LENGTH_SHIFT);
        } else {
            ret_val = e1000_read_phy_reg(hw, GG82563_PHY_DSP_DISTANCE,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_info->cable_length = (e1000_cable_length)(phy_data & GG82563_DSPD_CABLE_LENGTH);
        }

        ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_data);
        if (ret_val)
            return ret_val;

        phy_info->local_rx = ((phy_data & SR_1000T_LOCAL_RX_STATUS) >>
                             SR_1000T_LOCAL_RX_STATUS_SHIFT) ?
                             e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;
        phy_info->remote_rx = ((phy_data & SR_1000T_REMOTE_RX_STATUS) >>
                              SR_1000T_REMOTE_RX_STATUS_SHIFT) ?
                              e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;

    }

    return E1000_SUCCESS;
}

/******************************************************************************
* Get PHY information from various PHY registers
*
* hw - Struct containing variables accessed by shared code
* phy_info - PHY information structure
******************************************************************************/
int32_t
e1000_phy_get_info(struct e1000_hw *hw,
                   struct e1000_phy_info *phy_info)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_phy_get_info");

    phy_info->cable_length = e1000_cable_length_undefined;
    phy_info->extended_10bt_distance = e1000_10bt_ext_dist_enable_undefined;
    phy_info->cable_polarity = e1000_rev_polarity_undefined;
    phy_info->downshift = e1000_downshift_undefined;
    phy_info->polarity_correction = e1000_polarity_reversal_undefined;
    phy_info->mdix_mode = e1000_auto_x_mode_undefined;
    phy_info->local_rx = e1000_1000t_rx_status_undefined;
    phy_info->remote_rx = e1000_1000t_rx_status_undefined;

    if (hw->media_type != e1000_media_type_copper) {
        DEBUGOUT("PHY info is only valid for copper media\n");
        return -E1000_ERR_CONFIG;
    }

    ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
    if (ret_val)
        return ret_val;

    ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
    if (ret_val)
        return ret_val;

    if ((phy_data & MII_SR_LINK_STATUS) != MII_SR_LINK_STATUS) {
        DEBUGOUT("PHY info is only valid if link is up\n");
        return -E1000_ERR_CONFIG;
    }

    if (hw->phy_type == e1000_phy_igp ||
        hw->phy_type == e1000_phy_igp_3 ||
        hw->phy_type == e1000_phy_igp_2)
        return e1000_phy_igp_get_info(hw, phy_info);
    else if (hw->phy_type == e1000_phy_ife)
        return e1000_phy_ife_get_info(hw, phy_info);
    else
        return e1000_phy_m88_get_info(hw, phy_info);
}

int32_t
e1000_validate_mdi_setting(struct e1000_hw *hw)
{
    DEBUGFUNC("e1000_validate_mdi_settings");

    if (!hw->autoneg && (hw->mdix == 0 || hw->mdix == 3)) {
        DEBUGOUT("Invalid MDI setting detected\n");
        hw->mdix = 1;
        return -E1000_ERR_CONFIG;
    }
    return E1000_SUCCESS;
}


/******************************************************************************
 * Sets up eeprom variables in the hw struct.  Must be called after mac_type
 * is configured.  Additionally, if this is ICH8, the flash controller GbE
 * registers must be mapped, or this will crash.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_init_eeprom_params(struct e1000_hw *hw)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t eecd = E1000_READ_REG(hw, EECD);
    int32_t ret_val = E1000_SUCCESS;
    uint16_t eeprom_size;

    DEBUGFUNC("e1000_init_eeprom_params");

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
    case e1000_82543:
    case e1000_82544:
        eeprom->type = e1000_eeprom_microwire;
        eeprom->word_size = 64;
        eeprom->opcode_bits = 3;
        eeprom->address_bits = 6;
        eeprom->delay_usec = 50;
        eeprom->use_eerd = FALSE;
        eeprom->use_eewr = FALSE;
        break;
    case e1000_82540:
    case e1000_82545:
    case e1000_82545_rev_3:
    case e1000_82546:
    case e1000_82546_rev_3:
        eeprom->type = e1000_eeprom_microwire;
        eeprom->opcode_bits = 3;
        eeprom->delay_usec = 50;
        if (eecd & E1000_EECD_SIZE) {
            eeprom->word_size = 256;
            eeprom->address_bits = 8;
        } else {
            eeprom->word_size = 64;
            eeprom->address_bits = 6;
        }
        eeprom->use_eerd = FALSE;
        eeprom->use_eewr = FALSE;
        break;
    case e1000_82541:
    case e1000_82541_rev_2:
    case e1000_82547:
    case e1000_82547_rev_2:
        if (eecd & E1000_EECD_TYPE) {
            eeprom->type = e1000_eeprom_spi;
            eeprom->opcode_bits = 8;
            eeprom->delay_usec = 1;
            if (eecd & E1000_EECD_ADDR_BITS) {
                eeprom->page_size = 32;
                eeprom->address_bits = 16;
            } else {
                eeprom->page_size = 8;
                eeprom->address_bits = 8;
            }
        } else {
            eeprom->type = e1000_eeprom_microwire;
            eeprom->opcode_bits = 3;
            eeprom->delay_usec = 50;
            if (eecd & E1000_EECD_ADDR_BITS) {
                eeprom->word_size = 256;
                eeprom->address_bits = 8;
            } else {
                eeprom->word_size = 64;
                eeprom->address_bits = 6;
            }
        }
        eeprom->use_eerd = FALSE;
        eeprom->use_eewr = FALSE;
        break;
    case e1000_82571:
    case e1000_82572:
        eeprom->type = e1000_eeprom_spi;
        eeprom->opcode_bits = 8;
        eeprom->delay_usec = 1;
        if (eecd & E1000_EECD_ADDR_BITS) {
            eeprom->page_size = 32;
            eeprom->address_bits = 16;
        } else {
            eeprom->page_size = 8;
            eeprom->address_bits = 8;
        }
        eeprom->use_eerd = FALSE;
        eeprom->use_eewr = FALSE;
        break;
    case e1000_82573:
        eeprom->type = e1000_eeprom_spi;
        eeprom->opcode_bits = 8;
        eeprom->delay_usec = 1;
        if (eecd & E1000_EECD_ADDR_BITS) {
            eeprom->page_size = 32;
            eeprom->address_bits = 16;
        } else {
            eeprom->page_size = 8;
            eeprom->address_bits = 8;
        }
        eeprom->use_eerd = TRUE;
        eeprom->use_eewr = TRUE;
        if (e1000_is_onboard_nvm_eeprom(hw) == FALSE) {
            eeprom->type = e1000_eeprom_flash;
            eeprom->word_size = 2048;

            /* Ensure that the Autonomous FLASH update bit is cleared due to
             * Flash update issue on parts which use a FLASH for NVM. */
            eecd &= ~E1000_EECD_AUPDEN;
            E1000_WRITE_REG(hw, EECD, eecd);
        }
        break;
    case e1000_80003es2lan:
        eeprom->type = e1000_eeprom_spi;
        eeprom->opcode_bits = 8;
        eeprom->delay_usec = 1;
        if (eecd & E1000_EECD_ADDR_BITS) {
            eeprom->page_size = 32;
            eeprom->address_bits = 16;
        } else {
            eeprom->page_size = 8;
            eeprom->address_bits = 8;
        }
        eeprom->use_eerd = TRUE;
        eeprom->use_eewr = FALSE;
        break;
    case e1000_ich8lan:
        {
        int32_t  i = 0;
        uint32_t flash_size = E1000_READ_ICH8_REG(hw, ICH8_FLASH_GFPREG);

        eeprom->type = e1000_eeprom_ich8;
        eeprom->use_eerd = FALSE;
        eeprom->use_eewr = FALSE;
        eeprom->word_size = E1000_SHADOW_RAM_WORDS;

        /* Zero the shadow RAM structure. But don't load it from NVM
         * so as to save time for driver init */
        if (hw->eeprom_shadow_ram != NULL) {
            for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
                hw->eeprom_shadow_ram[i].modified = FALSE;
                hw->eeprom_shadow_ram[i].eeprom_word = 0xFFFF;
            }
        }

        hw->flash_base_addr = (flash_size & ICH8_GFPREG_BASE_MASK) *
                              ICH8_FLASH_SECTOR_SIZE;

        hw->flash_bank_size = ((flash_size >> 16) & ICH8_GFPREG_BASE_MASK) + 1;
        hw->flash_bank_size -= (flash_size & ICH8_GFPREG_BASE_MASK);
        hw->flash_bank_size *= ICH8_FLASH_SECTOR_SIZE;
        hw->flash_bank_size /= 2 * sizeof(uint16_t);

        break;
        }
    default:
        break;
    }

    if (eeprom->type == e1000_eeprom_spi) {
        /* eeprom_size will be an enum [0..8] that maps to eeprom sizes 128B to
         * 32KB (incremented by powers of 2).
         */
        if (hw->mac_type <= e1000_82547_rev_2) {
            /* Set to default value for initial eeprom read. */
            eeprom->word_size = 64;
            ret_val = e1000_read_eeprom(hw, EEPROM_CFG, 1, &eeprom_size);
            if (ret_val)
                return ret_val;
            eeprom_size = (eeprom_size & EEPROM_SIZE_MASK) >> EEPROM_SIZE_SHIFT;
            /* 256B eeprom size was not supported in earlier hardware, so we
             * bump eeprom_size up one to ensure that "1" (which maps to 256B)
             * is never the result used in the shifting logic below. */
            if (eeprom_size)
                eeprom_size++;
        } else {
            eeprom_size = (uint16_t)((eecd & E1000_EECD_SIZE_EX_MASK) >>
                          E1000_EECD_SIZE_EX_SHIFT);
        }

        eeprom->word_size = 1 << (eeprom_size + EEPROM_WORD_SIZE_SHIFT);
    }
    return ret_val;
}

/******************************************************************************
 * Raises the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd - EECD's current value
 *****************************************************************************/
static void
e1000_raise_ee_clk(struct e1000_hw *hw,
                   uint32_t *eecd)
{
    /* Raise the clock input to the EEPROM (by setting the SK bit), and then
     * wait <delay> microseconds.
     */
    *eecd = *eecd | E1000_EECD_SK;
    E1000_WRITE_REG(hw, EECD, *eecd);
    E1000_WRITE_FLUSH(hw);
    udelay(hw->eeprom.delay_usec);
}

/******************************************************************************
 * Lowers the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd - EECD's current value
 *****************************************************************************/
static void
e1000_lower_ee_clk(struct e1000_hw *hw,
                   uint32_t *eecd)
{
    /* Lower the clock input to the EEPROM (by clearing the SK bit), and then
     * wait 50 microseconds.
     */
    *eecd = *eecd & ~E1000_EECD_SK;
    E1000_WRITE_REG(hw, EECD, *eecd);
    E1000_WRITE_FLUSH(hw);
    udelay(hw->eeprom.delay_usec);
}

/******************************************************************************
 * Shift data bits out to the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * data - data to send to the EEPROM
 * count - number of bits to shift out
 *****************************************************************************/
static void
e1000_shift_out_ee_bits(struct e1000_hw *hw,
                        uint16_t data,
                        uint16_t count)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t eecd;
    uint32_t mask;

    /* We need to shift "count" bits out to the EEPROM. So, value in the
     * "data" parameter will be shifted out to the EEPROM one bit at a time.
     * In order to do this, "data" must be broken down into bits.
     */
    mask = 0x01 << (count - 1);
    eecd = E1000_READ_REG(hw, EECD);
    if (eeprom->type == e1000_eeprom_microwire) {
        eecd &= ~E1000_EECD_DO;
    } else if (eeprom->type == e1000_eeprom_spi) {
        eecd |= E1000_EECD_DO;
    }
    do {
        /* A "1" is shifted out to the EEPROM by setting bit "DI" to a "1",
         * and then raising and then lowering the clock (the SK bit controls
         * the clock input to the EEPROM).  A "0" is shifted out to the EEPROM
         * by setting "DI" to "0" and then raising and then lowering the clock.
         */
        eecd &= ~E1000_EECD_DI;

        if (data & mask)
            eecd |= E1000_EECD_DI;

        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);

        udelay(eeprom->delay_usec);

        e1000_raise_ee_clk(hw, &eecd);
        e1000_lower_ee_clk(hw, &eecd);

        mask = mask >> 1;

    } while (mask);

    /* We leave the "DI" bit set to "0" when we leave this routine. */
    eecd &= ~E1000_EECD_DI;
    E1000_WRITE_REG(hw, EECD, eecd);
}

/******************************************************************************
 * Shift data bits in from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static uint16_t
e1000_shift_in_ee_bits(struct e1000_hw *hw,
                       uint16_t count)
{
    uint32_t eecd;
    uint32_t i;
    uint16_t data;

    /* In order to read a register from the EEPROM, we need to shift 'count'
     * bits in from the EEPROM. Bits are "shifted in" by raising the clock
     * input to the EEPROM (setting the SK bit), and then reading the value of
     * the "DO" bit.  During this "shifting in" process the "DI" bit should
     * always be clear.
     */

    eecd = E1000_READ_REG(hw, EECD);

    eecd &= ~(E1000_EECD_DO | E1000_EECD_DI);
    data = 0;

    for (i = 0; i < count; i++) {
        data = data << 1;
        e1000_raise_ee_clk(hw, &eecd);

        eecd = E1000_READ_REG(hw, EECD);

        eecd &= ~(E1000_EECD_DI);
        if (eecd & E1000_EECD_DO)
            data |= 1;

        e1000_lower_ee_clk(hw, &eecd);
    }

    return data;
}

/******************************************************************************
 * Prepares EEPROM for access
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Lowers EEPROM clock. Clears input pin. Sets the chip select pin. This
 * function should be called before issuing a command to the EEPROM.
 *****************************************************************************/
static int32_t
e1000_acquire_eeprom(struct e1000_hw *hw)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t eecd, i=0;

    DEBUGFUNC("e1000_acquire_eeprom");

    if (e1000_swfw_sync_acquire(hw, E1000_SWFW_EEP_SM))
        return -E1000_ERR_SWFW_SYNC;
    eecd = E1000_READ_REG(hw, EECD);

    if (hw->mac_type != e1000_82573) {
        /* Request EEPROM Access */
        if (hw->mac_type > e1000_82544) {
            eecd |= E1000_EECD_REQ;
            E1000_WRITE_REG(hw, EECD, eecd);
            eecd = E1000_READ_REG(hw, EECD);
            while ((!(eecd & E1000_EECD_GNT)) &&
                  (i < E1000_EEPROM_GRANT_ATTEMPTS)) {
                i++;
                udelay(5);
                eecd = E1000_READ_REG(hw, EECD);
            }
            if (!(eecd & E1000_EECD_GNT)) {
                eecd &= ~E1000_EECD_REQ;
                E1000_WRITE_REG(hw, EECD, eecd);
                DEBUGOUT("Could not acquire EEPROM grant\n");
                e1000_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
                return -E1000_ERR_EEPROM;
            }
        }
    }

    /* Setup EEPROM for Read/Write */

    if (eeprom->type == e1000_eeprom_microwire) {
        /* Clear SK and DI */
        eecd &= ~(E1000_EECD_DI | E1000_EECD_SK);
        E1000_WRITE_REG(hw, EECD, eecd);

        /* Set CS */
        eecd |= E1000_EECD_CS;
        E1000_WRITE_REG(hw, EECD, eecd);
    } else if (eeprom->type == e1000_eeprom_spi) {
        /* Clear SK and CS */
        eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
        E1000_WRITE_REG(hw, EECD, eecd);
        udelay(1);
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Returns EEPROM to a "standby" state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
e1000_standby_eeprom(struct e1000_hw *hw)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t eecd;

    eecd = E1000_READ_REG(hw, EECD);

    if (eeprom->type == e1000_eeprom_microwire) {
        eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);

        /* Clock high */
        eecd |= E1000_EECD_SK;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);

        /* Select EEPROM */
        eecd |= E1000_EECD_CS;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);

        /* Clock low */
        eecd &= ~E1000_EECD_SK;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);
    } else if (eeprom->type == e1000_eeprom_spi) {
        /* Toggle CS to flush commands */
        eecd |= E1000_EECD_CS;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);
        eecd &= ~E1000_EECD_CS;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(eeprom->delay_usec);
    }
}

/******************************************************************************
 * Terminates a command by inverting the EEPROM's chip select pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
e1000_release_eeprom(struct e1000_hw *hw)
{
    uint32_t eecd;

    DEBUGFUNC("e1000_release_eeprom");

    eecd = E1000_READ_REG(hw, EECD);

    if (hw->eeprom.type == e1000_eeprom_spi) {
        eecd |= E1000_EECD_CS;  /* Pull CS high */
        eecd &= ~E1000_EECD_SK; /* Lower SCK */

        E1000_WRITE_REG(hw, EECD, eecd);

        udelay(hw->eeprom.delay_usec);
    } else if (hw->eeprom.type == e1000_eeprom_microwire) {
        /* cleanup eeprom */

        /* CS on Microwire is active-high */
        eecd &= ~(E1000_EECD_CS | E1000_EECD_DI);

        E1000_WRITE_REG(hw, EECD, eecd);

        /* Rising edge of clock */
        eecd |= E1000_EECD_SK;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(hw->eeprom.delay_usec);

        /* Falling edge of clock */
        eecd &= ~E1000_EECD_SK;
        E1000_WRITE_REG(hw, EECD, eecd);
        E1000_WRITE_FLUSH(hw);
        udelay(hw->eeprom.delay_usec);
    }

    /* Stop requesting EEPROM access */
    if (hw->mac_type > e1000_82544) {
        eecd &= ~E1000_EECD_REQ;
        E1000_WRITE_REG(hw, EECD, eecd);
    }

    e1000_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
e1000_spi_eeprom_ready(struct e1000_hw *hw)
{
    uint16_t retry_count = 0;
    uint8_t spi_stat_reg;

    DEBUGFUNC("e1000_spi_eeprom_ready");

    /* Read "Status Register" repeatedly until the LSB is cleared.  The
     * EEPROM will signal that the command has been completed by clearing
     * bit 0 of the internal status register.  If it's not cleared within
     * 5 milliseconds, then error out.
     */
    retry_count = 0;
    do {
        e1000_shift_out_ee_bits(hw, EEPROM_RDSR_OPCODE_SPI,
                                hw->eeprom.opcode_bits);
        spi_stat_reg = (uint8_t)e1000_shift_in_ee_bits(hw, 8);
        if (!(spi_stat_reg & EEPROM_STATUS_RDY_SPI))
            break;

        udelay(5);
        retry_count += 5;

        e1000_standby_eeprom(hw);
    } while (retry_count < EEPROM_MAX_RETRY_SPI);

    /* ATMEL SPI write time could vary from 0-20mSec on 3.3V devices (and
     * only 0-5mSec on 5V devices)
     */
    if (retry_count >= EEPROM_MAX_RETRY_SPI) {
        DEBUGOUT("SPI EEPROM Status error\n");
        return -E1000_ERR_EEPROM;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
int32_t
e1000_read_eeprom(struct e1000_hw *hw,
                  uint16_t offset,
                  uint16_t words,
                  uint16_t *data)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t i = 0;

    DEBUGFUNC("e1000_read_eeprom");

    /* If eeprom is not yet detected, do so now */
    if (eeprom->word_size == 0)
        e1000_init_eeprom_params(hw);

    /* A check for invalid values:  offset too large, too many words, and not
     * enough words.
     */
    if ((offset >= eeprom->word_size) || (words > eeprom->word_size - offset) ||
       (words == 0)) {
        DEBUGOUT2("\"words\" parameter out of bounds. Words = %d, size = %d\n", offset, eeprom->word_size);
        return -E1000_ERR_EEPROM;
    }

    /* EEPROM's that don't use EERD to read require us to bit-bang the SPI
     * directly. In this case, we need to acquire the EEPROM so that
     * FW or other port software does not interrupt.
     */
    if (e1000_is_onboard_nvm_eeprom(hw) == TRUE &&
        hw->eeprom.use_eerd == FALSE) {
        /* Prepare the EEPROM for bit-bang reading */
        if (e1000_acquire_eeprom(hw) != E1000_SUCCESS)
            return -E1000_ERR_EEPROM;
    }

    /* Eerd register EEPROM access requires no eeprom aquire/release */
    if (eeprom->use_eerd == TRUE)
        return e1000_read_eeprom_eerd(hw, offset, words, data);

    /* ICH EEPROM access is done via the ICH flash controller */
    if (eeprom->type == e1000_eeprom_ich8)
        return e1000_read_eeprom_ich8(hw, offset, words, data);

    /* Set up the SPI or Microwire EEPROM for bit-bang reading.  We have
     * acquired the EEPROM at this point, so any returns should relase it */
    if (eeprom->type == e1000_eeprom_spi) {
        uint16_t word_in;
        uint8_t read_opcode = EEPROM_READ_OPCODE_SPI;

        if (e1000_spi_eeprom_ready(hw)) {
            e1000_release_eeprom(hw);
            return -E1000_ERR_EEPROM;
        }

        e1000_standby_eeprom(hw);

        /* Some SPI eeproms use the 8th address bit embedded in the opcode */
        if ((eeprom->address_bits == 8) && (offset >= 128))
            read_opcode |= EEPROM_A8_OPCODE_SPI;

        /* Send the READ command (opcode + addr)  */
        e1000_shift_out_ee_bits(hw, read_opcode, eeprom->opcode_bits);
        e1000_shift_out_ee_bits(hw, (uint16_t)(offset*2), eeprom->address_bits);

        /* Read the data.  The address of the eeprom internally increments with
         * each byte (spi) being read, saving on the overhead of eeprom setup
         * and tear-down.  The address counter will roll over if reading beyond
         * the size of the eeprom, thus allowing the entire memory to be read
         * starting from any offset. */
        for (i = 0; i < words; i++) {
            word_in = e1000_shift_in_ee_bits(hw, 16);
            data[i] = (word_in >> 8) | (word_in << 8);
        }
    } else if (eeprom->type == e1000_eeprom_microwire) {
        for (i = 0; i < words; i++) {
            /* Send the READ command (opcode + addr)  */
            e1000_shift_out_ee_bits(hw, EEPROM_READ_OPCODE_MICROWIRE,
                                    eeprom->opcode_bits);
            e1000_shift_out_ee_bits(hw, (uint16_t)(offset + i),
                                    eeprom->address_bits);

            /* Read the data.  For microwire, each word requires the overhead
             * of eeprom setup and tear-down. */
            data[i] = e1000_shift_in_ee_bits(hw, 16);
            e1000_standby_eeprom(hw);
        }
    }

    /* End this read operation */
    e1000_release_eeprom(hw);

    return E1000_SUCCESS;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM using the EERD register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
static int32_t
e1000_read_eeprom_eerd(struct e1000_hw *hw,
                  uint16_t offset,
                  uint16_t words,
                  uint16_t *data)
{
    uint32_t i, eerd = 0;
    int32_t error = 0;

    for (i = 0; i < words; i++) {
        eerd = ((offset+i) << E1000_EEPROM_RW_ADDR_SHIFT) +
                         E1000_EEPROM_RW_REG_START;

        E1000_WRITE_REG(hw, EERD, eerd);
        error = e1000_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_READ);

        if (error) {
            break;
        }
        data[i] = (E1000_READ_REG(hw, EERD) >> E1000_EEPROM_RW_REG_DATA);

    }

    return error;
}

/******************************************************************************
 * Writes a 16 bit word from the EEPROM using the EEWR register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
static int32_t
e1000_write_eeprom_eewr(struct e1000_hw *hw,
                   uint16_t offset,
                   uint16_t words,
                   uint16_t *data)
{
    uint32_t    register_value = 0;
    uint32_t    i              = 0;
    int32_t     error          = 0;

    if (e1000_swfw_sync_acquire(hw, E1000_SWFW_EEP_SM))
        return -E1000_ERR_SWFW_SYNC;

    for (i = 0; i < words; i++) {
        register_value = (data[i] << E1000_EEPROM_RW_REG_DATA) |
                         ((offset+i) << E1000_EEPROM_RW_ADDR_SHIFT) |
                         E1000_EEPROM_RW_REG_START;

        error = e1000_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_WRITE);
        if (error) {
            break;
        }

        E1000_WRITE_REG(hw, EEWR, register_value);

        error = e1000_poll_eerd_eewr_done(hw, E1000_EEPROM_POLL_WRITE);

        if (error) {
            break;
        }
    }

    e1000_swfw_sync_release(hw, E1000_SWFW_EEP_SM);
    return error;
}

/******************************************************************************
 * Polls the status bit (bit 1) of the EERD to determine when the read is done.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
e1000_poll_eerd_eewr_done(struct e1000_hw *hw, int eerd)
{
    uint32_t attempts = 100000;
    uint32_t i, reg = 0;
    int32_t done = E1000_ERR_EEPROM;

    for (i = 0; i < attempts; i++) {
        if (eerd == E1000_EEPROM_POLL_READ)
            reg = E1000_READ_REG(hw, EERD);
        else
            reg = E1000_READ_REG(hw, EEWR);

        if (reg & E1000_EEPROM_RW_REG_DONE) {
            done = E1000_SUCCESS;
            break;
        }
        udelay(5);
    }

    return done;
}

/***************************************************************************
* Description:     Determines if the onboard NVM is FLASH or EEPROM.
*
* hw - Struct containing variables accessed by shared code
****************************************************************************/
static boolean_t
e1000_is_onboard_nvm_eeprom(struct e1000_hw *hw)
{
    uint32_t eecd = 0;

    DEBUGFUNC("e1000_is_onboard_nvm_eeprom");

    if (hw->mac_type == e1000_ich8lan)
        return FALSE;

    if (hw->mac_type == e1000_82573) {
        eecd = E1000_READ_REG(hw, EECD);

        /* Isolate bits 15 & 16 */
        eecd = ((eecd >> 15) & 0x03);

        /* If both bits are set, device is Flash type */
        if (eecd == 0x03) {
            return FALSE;
        }
    }
    return TRUE;
}

/******************************************************************************
 * Verifies that the EEPROM has a valid checksum
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 *****************************************************************************/
int32_t
e1000_validate_eeprom_checksum(struct e1000_hw *hw)
{
    uint16_t checksum = 0;
    uint16_t i, eeprom_data;

    DEBUGFUNC("e1000_validate_eeprom_checksum");

    if ((hw->mac_type == e1000_82573) &&
        (e1000_is_onboard_nvm_eeprom(hw) == FALSE)) {
        /* Check bit 4 of word 10h.  If it is 0, firmware is done updating
         * 10h-12h.  Checksum may need to be fixed. */
        e1000_read_eeprom(hw, 0x10, 1, &eeprom_data);
        if ((eeprom_data & 0x10) == 0) {
            /* Read 0x23 and check bit 15.  This bit is a 1 when the checksum
             * has already been fixed.  If the checksum is still wrong and this
             * bit is a 1, we need to return bad checksum.  Otherwise, we need
             * to set this bit to a 1 and update the checksum. */
            e1000_read_eeprom(hw, 0x23, 1, &eeprom_data);
            if ((eeprom_data & 0x8000) == 0) {
                eeprom_data |= 0x8000;
                e1000_write_eeprom(hw, 0x23, 1, &eeprom_data);
                e1000_update_eeprom_checksum(hw);
            }
        }
    }

    if (hw->mac_type == e1000_ich8lan) {
        /* Drivers must allocate the shadow ram structure for the
         * EEPROM checksum to be updated.  Otherwise, this bit as well
         * as the checksum must both be set correctly for this
         * validation to pass.
         */
        e1000_read_eeprom(hw, 0x19, 1, &eeprom_data);
        if ((eeprom_data & 0x40) == 0) {
            eeprom_data |= 0x40;
            e1000_write_eeprom(hw, 0x19, 1, &eeprom_data);
            e1000_update_eeprom_checksum(hw);
        }
    }

    for (i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++) {
        if (e1000_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
            DEBUGOUT("EEPROM Read Error\n");
            return -E1000_ERR_EEPROM;
        }
        checksum += eeprom_data;
    }

    if (checksum == (uint16_t) EEPROM_SUM)
        return E1000_SUCCESS;
    else {
        DEBUGOUT("EEPROM Checksum Invalid\n");
        return -E1000_ERR_EEPROM;
    }
}

/******************************************************************************
 * Calculates the EEPROM checksum and writes it to the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 *****************************************************************************/
int32_t
e1000_update_eeprom_checksum(struct e1000_hw *hw)
{
    uint32_t ctrl_ext;
    uint16_t checksum = 0;
    uint16_t i, eeprom_data;

    DEBUGFUNC("e1000_update_eeprom_checksum");

    for (i = 0; i < EEPROM_CHECKSUM_REG; i++) {
        if (e1000_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
            DEBUGOUT("EEPROM Read Error\n");
            return -E1000_ERR_EEPROM;
        }
        checksum += eeprom_data;
    }
    checksum = (uint16_t) EEPROM_SUM - checksum;
    if (e1000_write_eeprom(hw, EEPROM_CHECKSUM_REG, 1, &checksum) < 0) {
        DEBUGOUT("EEPROM Write Error\n");
        return -E1000_ERR_EEPROM;
    } else if (hw->eeprom.type == e1000_eeprom_flash) {
        e1000_commit_shadow_ram(hw);
    } else if (hw->eeprom.type == e1000_eeprom_ich8) {
        e1000_commit_shadow_ram(hw);
        /* Reload the EEPROM, or else modifications will not appear
         * until after next adapter reset. */
        ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
        ctrl_ext |= E1000_CTRL_EXT_EE_RST;
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
        msleep(10);
    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Parent function for writing words to the different EEPROM types.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - 16 bit word to be written to the EEPROM
 *
 * If e1000_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 *****************************************************************************/
int32_t
e1000_write_eeprom(struct e1000_hw *hw,
                   uint16_t offset,
                   uint16_t words,
                   uint16_t *data)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    int32_t status = 0;

    DEBUGFUNC("e1000_write_eeprom");

    /* If eeprom is not yet detected, do so now */
    if (eeprom->word_size == 0)
        e1000_init_eeprom_params(hw);

    /* A check for invalid values:  offset too large, too many words, and not
     * enough words.
     */
    if ((offset >= eeprom->word_size) || (words > eeprom->word_size - offset) ||
       (words == 0)) {
        DEBUGOUT("\"words\" parameter out of bounds\n");
        return -E1000_ERR_EEPROM;
    }

    /* 82573 writes only through eewr */
    if (eeprom->use_eewr == TRUE)
        return e1000_write_eeprom_eewr(hw, offset, words, data);

    if (eeprom->type == e1000_eeprom_ich8)
        return e1000_write_eeprom_ich8(hw, offset, words, data);

    /* Prepare the EEPROM for writing  */
    if (e1000_acquire_eeprom(hw) != E1000_SUCCESS)
        return -E1000_ERR_EEPROM;

    if (eeprom->type == e1000_eeprom_microwire) {
        status = e1000_write_eeprom_microwire(hw, offset, words, data);
    } else {
        status = e1000_write_eeprom_spi(hw, offset, words, data);
        msleep(10);
    }

    /* Done with writing */
    e1000_release_eeprom(hw);

    return status;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in an SPI EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - pointer to array of 8 bit words to be written to the EEPROM
 *
 *****************************************************************************/
static int32_t
e1000_write_eeprom_spi(struct e1000_hw *hw,
                       uint16_t offset,
                       uint16_t words,
                       uint16_t *data)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint16_t widx = 0;

    DEBUGFUNC("e1000_write_eeprom_spi");

    while (widx < words) {
        uint8_t write_opcode = EEPROM_WRITE_OPCODE_SPI;

        if (e1000_spi_eeprom_ready(hw)) return -E1000_ERR_EEPROM;

        e1000_standby_eeprom(hw);

        /*  Send the WRITE ENABLE command (8 bit opcode )  */
        e1000_shift_out_ee_bits(hw, EEPROM_WREN_OPCODE_SPI,
                                    eeprom->opcode_bits);

        e1000_standby_eeprom(hw);

        /* Some SPI eeproms use the 8th address bit embedded in the opcode */
        if ((eeprom->address_bits == 8) && (offset >= 128))
            write_opcode |= EEPROM_A8_OPCODE_SPI;

        /* Send the Write command (8-bit opcode + addr) */
        e1000_shift_out_ee_bits(hw, write_opcode, eeprom->opcode_bits);

        e1000_shift_out_ee_bits(hw, (uint16_t)((offset + widx)*2),
                                eeprom->address_bits);

        /* Send the data */

        /* Loop to allow for up to whole page write (32 bytes) of eeprom */
        while (widx < words) {
            uint16_t word_out = data[widx];
            word_out = (word_out >> 8) | (word_out << 8);
            e1000_shift_out_ee_bits(hw, word_out, 16);
            widx++;

            /* Some larger eeprom sizes are capable of a 32-byte PAGE WRITE
             * operation, while the smaller eeproms are capable of an 8-byte
             * PAGE WRITE operation.  Break the inner loop to pass new address
             */
            if ((((offset + widx)*2) % eeprom->page_size) == 0) {
                e1000_standby_eeprom(hw);
                break;
            }
        }
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in a Microwire EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset within the EEPROM to be written to
 * words - number of words to write
 * data - pointer to array of 16 bit words to be written to the EEPROM
 *
 *****************************************************************************/
static int32_t
e1000_write_eeprom_microwire(struct e1000_hw *hw,
                             uint16_t offset,
                             uint16_t words,
                             uint16_t *data)
{
    struct e1000_eeprom_info *eeprom = &hw->eeprom;
    uint32_t eecd;
    uint16_t words_written = 0;
    uint16_t i = 0;

    DEBUGFUNC("e1000_write_eeprom_microwire");

    /* Send the write enable command to the EEPROM (3-bit opcode plus
     * 6/8-bit dummy address beginning with 11).  It's less work to include
     * the 11 of the dummy address as part of the opcode than it is to shift
     * it over the correct number of bits for the address.  This puts the
     * EEPROM into write/erase mode.
     */
    e1000_shift_out_ee_bits(hw, EEPROM_EWEN_OPCODE_MICROWIRE,
                            (uint16_t)(eeprom->opcode_bits + 2));

    e1000_shift_out_ee_bits(hw, 0, (uint16_t)(eeprom->address_bits - 2));

    /* Prepare the EEPROM */
    e1000_standby_eeprom(hw);

    while (words_written < words) {
        /* Send the Write command (3-bit opcode + addr) */
        e1000_shift_out_ee_bits(hw, EEPROM_WRITE_OPCODE_MICROWIRE,
                                eeprom->opcode_bits);

        e1000_shift_out_ee_bits(hw, (uint16_t)(offset + words_written),
                                eeprom->address_bits);

        /* Send the data */
        e1000_shift_out_ee_bits(hw, data[words_written], 16);

        /* Toggle the CS line.  This in effect tells the EEPROM to execute
         * the previous command.
         */
        e1000_standby_eeprom(hw);

        /* Read DO repeatedly until it is high (equal to '1').  The EEPROM will
         * signal that the command has been completed by raising the DO signal.
         * If DO does not go high in 10 milliseconds, then error out.
         */
        for (i = 0; i < 200; i++) {
            eecd = E1000_READ_REG(hw, EECD);
            if (eecd & E1000_EECD_DO) break;
            udelay(50);
        }
        if (i == 200) {
            DEBUGOUT("EEPROM Write did not complete\n");
            return -E1000_ERR_EEPROM;
        }

        /* Recover from write */
        e1000_standby_eeprom(hw);

        words_written++;
    }

    /* Send the write disable command to the EEPROM (3-bit opcode plus
     * 6/8-bit dummy address beginning with 10).  It's less work to include
     * the 10 of the dummy address as part of the opcode than it is to shift
     * it over the correct number of bits for the address.  This takes the
     * EEPROM out of write/erase mode.
     */
    e1000_shift_out_ee_bits(hw, EEPROM_EWDS_OPCODE_MICROWIRE,
                            (uint16_t)(eeprom->opcode_bits + 2));

    e1000_shift_out_ee_bits(hw, 0, (uint16_t)(eeprom->address_bits - 2));

    return E1000_SUCCESS;
}

/******************************************************************************
 * Flushes the cached eeprom to NVM. This is done by saving the modified values
 * in the eeprom cache and the non modified values in the currently active bank
 * to the new bank.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of  word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
static int32_t
e1000_commit_shadow_ram(struct e1000_hw *hw)
{
    uint32_t attempts = 100000;
    uint32_t eecd = 0;
    uint32_t flop = 0;
    uint32_t i = 0;
    int32_t error = E1000_SUCCESS;
    uint32_t old_bank_offset = 0;
    uint32_t new_bank_offset = 0;
    uint8_t low_byte = 0;
    uint8_t high_byte = 0;
    boolean_t sector_write_failed = FALSE;

    if (hw->mac_type == e1000_82573) {
        /* The flop register will be used to determine if flash type is STM */
        flop = E1000_READ_REG(hw, FLOP);
        for (i=0; i < attempts; i++) {
            eecd = E1000_READ_REG(hw, EECD);
            if ((eecd & E1000_EECD_FLUPD) == 0) {
                break;
            }
            udelay(5);
        }

        if (i == attempts) {
            return -E1000_ERR_EEPROM;
        }

        /* If STM opcode located in bits 15:8 of flop, reset firmware */
        if ((flop & 0xFF00) == E1000_STM_OPCODE) {
            E1000_WRITE_REG(hw, HICR, E1000_HICR_FW_RESET);
        }

        /* Perform the flash update */
        E1000_WRITE_REG(hw, EECD, eecd | E1000_EECD_FLUPD);

        for (i=0; i < attempts; i++) {
            eecd = E1000_READ_REG(hw, EECD);
            if ((eecd & E1000_EECD_FLUPD) == 0) {
                break;
            }
            udelay(5);
        }

        if (i == attempts) {
            return -E1000_ERR_EEPROM;
        }
    }

    if (hw->mac_type == e1000_ich8lan && hw->eeprom_shadow_ram != NULL) {
        /* We're writing to the opposite bank so if we're on bank 1,
         * write to bank 0 etc.  We also need to erase the segment that
         * is going to be written */
        if (!(E1000_READ_REG(hw, EECD) & E1000_EECD_SEC1VAL)) {
            new_bank_offset = hw->flash_bank_size * 2;
            old_bank_offset = 0;
            e1000_erase_ich8_4k_segment(hw, 1);
        } else {
            old_bank_offset = hw->flash_bank_size * 2;
            new_bank_offset = 0;
            e1000_erase_ich8_4k_segment(hw, 0);
        }

        sector_write_failed = FALSE;
        /* Loop for every byte in the shadow RAM,
         * which is in units of words. */
        for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
            /* Determine whether to write the value stored
             * in the other NVM bank or a modified value stored
             * in the shadow RAM */
            if (hw->eeprom_shadow_ram[i].modified == TRUE) {
                low_byte = (uint8_t)hw->eeprom_shadow_ram[i].eeprom_word;
                udelay(100);
                error = e1000_verify_write_ich8_byte(hw,
                            (i << 1) + new_bank_offset, low_byte);

                if (error != E1000_SUCCESS)
                    sector_write_failed = TRUE;
                else {
                    high_byte =
                        (uint8_t)(hw->eeprom_shadow_ram[i].eeprom_word >> 8);
                    udelay(100);
                }
            } else {
                e1000_read_ich8_byte(hw, (i << 1) + old_bank_offset,
                                     &low_byte);
                udelay(100);
                error = e1000_verify_write_ich8_byte(hw,
                            (i << 1) + new_bank_offset, low_byte);

                if (error != E1000_SUCCESS)
                    sector_write_failed = TRUE;
                else {
                    e1000_read_ich8_byte(hw, (i << 1) + old_bank_offset + 1,
                                         &high_byte);
                    udelay(100);
                }
            }

            /* If the write of the low byte was successful, go ahread and
             * write the high byte while checking to make sure that if it
             * is the signature byte, then it is handled properly */
            if (sector_write_failed == FALSE) {
                /* If the word is 0x13, then make sure the signature bits
                 * (15:14) are 11b until the commit has completed.
                 * This will allow us to write 10b which indicates the
                 * signature is valid.  We want to do this after the write
                 * has completed so that we don't mark the segment valid
                 * while the write is still in progress */
                if (i == E1000_ICH8_NVM_SIG_WORD)
                    high_byte = E1000_ICH8_NVM_SIG_MASK | high_byte;

                error = e1000_verify_write_ich8_byte(hw,
                            (i << 1) + new_bank_offset + 1, high_byte);
                if (error != E1000_SUCCESS)
                    sector_write_failed = TRUE;

            } else {
                /* If the write failed then break from the loop and
                 * return an error */
                break;
            }
        }

        /* Don't bother writing the segment valid bits if sector
         * programming failed. */
        if (sector_write_failed == FALSE) {
            /* Finally validate the new segment by setting bit 15:14
             * to 10b in word 0x13 , this can be done without an
             * erase as well since these bits are 11 to start with
             * and we need to change bit 14 to 0b */
            e1000_read_ich8_byte(hw,
                                 E1000_ICH8_NVM_SIG_WORD * 2 + 1 + new_bank_offset,
                                 &high_byte);
            high_byte &= 0xBF;
            error = e1000_verify_write_ich8_byte(hw,
                        E1000_ICH8_NVM_SIG_WORD * 2 + 1 + new_bank_offset, high_byte);
            /* And invalidate the previously valid segment by setting
             * its signature word (0x13) high_byte to 0b. This can be
             * done without an erase because flash erase sets all bits
             * to 1's. We can write 1's to 0's without an erase */
            if (error == E1000_SUCCESS) {
                error = e1000_verify_write_ich8_byte(hw,
                            E1000_ICH8_NVM_SIG_WORD * 2 + 1 + old_bank_offset, 0);
            }

            /* Clear the now not used entry in the cache */
            for (i = 0; i < E1000_SHADOW_RAM_WORDS; i++) {
                hw->eeprom_shadow_ram[i].modified = FALSE;
                hw->eeprom_shadow_ram[i].eeprom_word = 0xFFFF;
            }
        }
    }

    return error;
}

/******************************************************************************
 * Reads the adapter's MAC address from the EEPROM and inverts the LSB for the
 * second function of dual function devices
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_read_mac_addr(struct e1000_hw * hw)
{
    uint16_t offset;
    uint16_t eeprom_data, i;

    DEBUGFUNC("e1000_read_mac_addr");

    for (i = 0; i < NODE_ADDRESS_SIZE; i += 2) {
        offset = i >> 1;
        if (e1000_read_eeprom(hw, offset, 1, &eeprom_data) < 0) {
            DEBUGOUT("EEPROM Read Error\n");
            return -E1000_ERR_EEPROM;
        }
        hw->perm_mac_addr[i] = (uint8_t) (eeprom_data & 0x00FF);
        hw->perm_mac_addr[i+1] = (uint8_t) (eeprom_data >> 8);
    }

    switch (hw->mac_type) {
    default:
        break;
    case e1000_82546:
    case e1000_82546_rev_3:
    case e1000_82571:
    case e1000_80003es2lan:
        if (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)
            hw->perm_mac_addr[5] ^= 0x01;
        break;
    }

    for (i = 0; i < NODE_ADDRESS_SIZE; i++)
        hw->mac_addr[i] = hw->perm_mac_addr[i];
    return E1000_SUCCESS;
}

/******************************************************************************
 * Initializes receive address filters.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Places the MAC address in receive address register 0 and clears the rest
 * of the receive addresss registers. Clears the multicast table. Assumes
 * the receiver is in reset when the routine is called.
 *****************************************************************************/
static void
e1000_init_rx_addrs(struct e1000_hw *hw)
{
    uint32_t i;
    uint32_t rar_num;

    DEBUGFUNC("e1000_init_rx_addrs");

    /* Setup the receive address. */
    DEBUGOUT("Programming MAC Address into RAR[0]\n");

    e1000_rar_set(hw, hw->mac_addr, 0);

    rar_num = E1000_RAR_ENTRIES;

    /* Reserve a spot for the Locally Administered Address to work around
     * an 82571 issue in which a reset on one port will reload the MAC on
     * the other port. */
    if ((hw->mac_type == e1000_82571) && (hw->laa_is_present == TRUE))
        rar_num -= 1;
    if (hw->mac_type == e1000_ich8lan)
        rar_num = E1000_RAR_ENTRIES_ICH8LAN;

    /* Zero out the other 15 receive addresses. */
    DEBUGOUT("Clearing RAR[1-15]\n");
    for (i = 1; i < rar_num; i++) {
        E1000_WRITE_REG_ARRAY(hw, RA, (i << 1), 0);
        E1000_WRITE_FLUSH(hw);
        E1000_WRITE_REG_ARRAY(hw, RA, ((i << 1) + 1), 0);
        E1000_WRITE_FLUSH(hw);
    }
}

/******************************************************************************
 * Hashes an address to determine its location in the multicast table
 *
 * hw - Struct containing variables accessed by shared code
 * mc_addr - the multicast address to hash
 *****************************************************************************/
uint32_t
e1000_hash_mc_addr(struct e1000_hw *hw,
                   uint8_t *mc_addr)
{
    uint32_t hash_value = 0;

    /* The portion of the address that is used for the hash table is
     * determined by the mc_filter_type setting.
     */
    switch (hw->mc_filter_type) {
    /* [0] [1] [2] [3] [4] [5]
     * 01  AA  00  12  34  56
     * LSB                 MSB
     */
    case 0:
        if (hw->mac_type == e1000_ich8lan) {
            /* [47:38] i.e. 0x158 for above example address */
            hash_value = ((mc_addr[4] >> 6) | (((uint16_t) mc_addr[5]) << 2));
        } else {
            /* [47:36] i.e. 0x563 for above example address */
            hash_value = ((mc_addr[4] >> 4) | (((uint16_t) mc_addr[5]) << 4));
        }
        break;
    case 1:
        if (hw->mac_type == e1000_ich8lan) {
            /* [46:37] i.e. 0x2B1 for above example address */
            hash_value = ((mc_addr[4] >> 5) | (((uint16_t) mc_addr[5]) << 3));
        } else {
            /* [46:35] i.e. 0xAC6 for above example address */
            hash_value = ((mc_addr[4] >> 3) | (((uint16_t) mc_addr[5]) << 5));
        }
        break;
    case 2:
        if (hw->mac_type == e1000_ich8lan) {
            /*[45:36] i.e. 0x163 for above example address */
            hash_value = ((mc_addr[4] >> 4) | (((uint16_t) mc_addr[5]) << 4));
        } else {
            /* [45:34] i.e. 0x5D8 for above example address */
            hash_value = ((mc_addr[4] >> 2) | (((uint16_t) mc_addr[5]) << 6));
        }
        break;
    case 3:
        if (hw->mac_type == e1000_ich8lan) {
            /* [43:34] i.e. 0x18D for above example address */
            hash_value = ((mc_addr[4] >> 2) | (((uint16_t) mc_addr[5]) << 6));
        } else {
            /* [43:32] i.e. 0x634 for above example address */
            hash_value = ((mc_addr[4]) | (((uint16_t) mc_addr[5]) << 8));
        }
        break;
    }

    hash_value &= 0xFFF;
    if (hw->mac_type == e1000_ich8lan)
        hash_value &= 0x3FF;

    return hash_value;
}

/******************************************************************************
 * Sets the bit in the multicast table corresponding to the hash value.
 *
 * hw - Struct containing variables accessed by shared code
 * hash_value - Multicast address hash value
 *****************************************************************************/
void
e1000_mta_set(struct e1000_hw *hw,
              uint32_t hash_value)
{
    uint32_t hash_bit, hash_reg;
    uint32_t mta;
    uint32_t temp;

    /* The MTA is a register array of 128 32-bit registers.
     * It is treated like an array of 4096 bits.  We want to set
     * bit BitArray[hash_value]. So we figure out what register
     * the bit is in, read it, OR in the new bit, then write
     * back the new value.  The register is determined by the
     * upper 7 bits of the hash value and the bit within that
     * register are determined by the lower 5 bits of the value.
     */
    hash_reg = (hash_value >> 5) & 0x7F;
    if (hw->mac_type == e1000_ich8lan)
        hash_reg &= 0x1F;

    hash_bit = hash_value & 0x1F;

    mta = E1000_READ_REG_ARRAY(hw, MTA, hash_reg);

    mta |= (1 << hash_bit);

    /* If we are on an 82544 and we are trying to write an odd offset
     * in the MTA, save off the previous entry before writing and
     * restore the old value after writing.
     */
    if ((hw->mac_type == e1000_82544) && ((hash_reg & 0x1) == 1)) {
        temp = E1000_READ_REG_ARRAY(hw, MTA, (hash_reg - 1));
        E1000_WRITE_REG_ARRAY(hw, MTA, hash_reg, mta);
        E1000_WRITE_FLUSH(hw);
        E1000_WRITE_REG_ARRAY(hw, MTA, (hash_reg - 1), temp);
        E1000_WRITE_FLUSH(hw);
    } else {
        E1000_WRITE_REG_ARRAY(hw, MTA, hash_reg, mta);
        E1000_WRITE_FLUSH(hw);
    }
}

/******************************************************************************
 * Puts an ethernet address into a receive address register.
 *
 * hw - Struct containing variables accessed by shared code
 * addr - Address to put into receive address register
 * index - Receive address register to write
 *****************************************************************************/
void
e1000_rar_set(struct e1000_hw *hw,
              uint8_t *addr,
              uint32_t index)
{
    uint32_t rar_low, rar_high;

    /* HW expects these in little endian so we reverse the byte order
     * from network order (big endian) to little endian
     */
    rar_low = ((uint32_t) addr[0] |
               ((uint32_t) addr[1] << 8) |
               ((uint32_t) addr[2] << 16) | ((uint32_t) addr[3] << 24));
    rar_high = ((uint32_t) addr[4] | ((uint32_t) addr[5] << 8));

    /* Disable Rx and flush all Rx frames before enabling RSS to avoid Rx
     * unit hang.
     *
     * Description:
     * If there are any Rx frames queued up or otherwise present in the HW
     * before RSS is enabled, and then we enable RSS, the HW Rx unit will
     * hang.  To work around this issue, we have to disable receives and
     * flush out all Rx frames before we enable RSS. To do so, we modify we
     * redirect all Rx traffic to manageability and then reset the HW.
     * This flushes away Rx frames, and (since the redirections to
     * manageability persists across resets) keeps new ones from coming in
     * while we work.  Then, we clear the Address Valid AV bit for all MAC
     * addresses and undo the re-direction to manageability.
     * Now, frames are coming in again, but the MAC won't accept them, so
     * far so good.  We now proceed to initialize RSS (if necessary) and
     * configure the Rx unit.  Last, we re-enable the AV bits and continue
     * on our merry way.
     */
    switch (hw->mac_type) {
    case e1000_82571:
    case e1000_82572:
    case e1000_80003es2lan:
        if (hw->leave_av_bit_off == TRUE)
            break;
    default:
        /* Indicate to hardware the Address is Valid. */
        rar_high |= E1000_RAH_AV;
        break;
    }

    E1000_WRITE_REG_ARRAY(hw, RA, (index << 1), rar_low);
    E1000_WRITE_FLUSH(hw);
    E1000_WRITE_REG_ARRAY(hw, RA, ((index << 1) + 1), rar_high);
    E1000_WRITE_FLUSH(hw);
}

/******************************************************************************
 * Writes a value to the specified offset in the VLAN filter table.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - Offset in VLAN filer table to write
 * value - Value to write into VLAN filter table
 *****************************************************************************/
void
e1000_write_vfta(struct e1000_hw *hw,
                 uint32_t offset,
                 uint32_t value)
{
    uint32_t temp;

    if (hw->mac_type == e1000_ich8lan)
        return;

    if ((hw->mac_type == e1000_82544) && ((offset & 0x1) == 1)) {
        temp = E1000_READ_REG_ARRAY(hw, VFTA, (offset - 1));
        E1000_WRITE_REG_ARRAY(hw, VFTA, offset, value);
        E1000_WRITE_FLUSH(hw);
        E1000_WRITE_REG_ARRAY(hw, VFTA, (offset - 1), temp);
        E1000_WRITE_FLUSH(hw);
    } else {
        E1000_WRITE_REG_ARRAY(hw, VFTA, offset, value);
        E1000_WRITE_FLUSH(hw);
    }
}

/******************************************************************************
 * Clears the VLAN filer table
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
e1000_clear_vfta(struct e1000_hw *hw)
{
    uint32_t offset;
    uint32_t vfta_value = 0;
    uint32_t vfta_offset = 0;
    uint32_t vfta_bit_in_reg = 0;

    if (hw->mac_type == e1000_ich8lan)
        return;

    if (hw->mac_type == e1000_82573) {
        if (hw->mng_cookie.vlan_id != 0) {
            /* The VFTA is a 4096b bit-field, each identifying a single VLAN
             * ID.  The following operations determine which 32b entry
             * (i.e. offset) into the array we want to set the VLAN ID
             * (i.e. bit) of the manageability unit. */
            vfta_offset = (hw->mng_cookie.vlan_id >>
                           E1000_VFTA_ENTRY_SHIFT) &
                          E1000_VFTA_ENTRY_MASK;
            vfta_bit_in_reg = 1 << (hw->mng_cookie.vlan_id &
                                    E1000_VFTA_ENTRY_BIT_SHIFT_MASK);
        }
    }
    for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
        /* If the offset we want to clear is the same offset of the
         * manageability VLAN ID, then clear all bits except that of the
         * manageability unit */
        vfta_value = (offset == vfta_offset) ? vfta_bit_in_reg : 0;
        E1000_WRITE_REG_ARRAY(hw, VFTA, offset, vfta_value);
        E1000_WRITE_FLUSH(hw);
    }
}

static int32_t
e1000_id_led_init(struct e1000_hw * hw)
{
    uint32_t ledctl;
    const uint32_t ledctl_mask = 0x000000FF;
    const uint32_t ledctl_on = E1000_LEDCTL_MODE_LED_ON;
    const uint32_t ledctl_off = E1000_LEDCTL_MODE_LED_OFF;
    uint16_t eeprom_data, i, temp;
    const uint16_t led_mask = 0x0F;

    DEBUGFUNC("e1000_id_led_init");

    if (hw->mac_type < e1000_82540) {
        /* Nothing to do */
        return E1000_SUCCESS;
    }

    ledctl = E1000_READ_REG(hw, LEDCTL);
    hw->ledctl_default = ledctl;
    hw->ledctl_mode1 = hw->ledctl_default;
    hw->ledctl_mode2 = hw->ledctl_default;

    if (e1000_read_eeprom(hw, EEPROM_ID_LED_SETTINGS, 1, &eeprom_data) < 0) {
        DEBUGOUT("EEPROM Read Error\n");
        return -E1000_ERR_EEPROM;
    }

    if ((hw->mac_type == e1000_82573) &&
        (eeprom_data == ID_LED_RESERVED_82573))
        eeprom_data = ID_LED_DEFAULT_82573;
    else if ((eeprom_data == ID_LED_RESERVED_0000) ||
            (eeprom_data == ID_LED_RESERVED_FFFF)) {
        if (hw->mac_type == e1000_ich8lan)
            eeprom_data = ID_LED_DEFAULT_ICH8LAN;
        else
            eeprom_data = ID_LED_DEFAULT;
    }

    for (i = 0; i < 4; i++) {
        temp = (eeprom_data >> (i << 2)) & led_mask;
        switch (temp) {
        case ID_LED_ON1_DEF2:
        case ID_LED_ON1_ON2:
        case ID_LED_ON1_OFF2:
            hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
            hw->ledctl_mode1 |= ledctl_on << (i << 3);
            break;
        case ID_LED_OFF1_DEF2:
        case ID_LED_OFF1_ON2:
        case ID_LED_OFF1_OFF2:
            hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
            hw->ledctl_mode1 |= ledctl_off << (i << 3);
            break;
        default:
            /* Do nothing */
            break;
        }
        switch (temp) {
        case ID_LED_DEF1_ON2:
        case ID_LED_ON1_ON2:
        case ID_LED_OFF1_ON2:
            hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
            hw->ledctl_mode2 |= ledctl_on << (i << 3);
            break;
        case ID_LED_DEF1_OFF2:
        case ID_LED_ON1_OFF2:
        case ID_LED_OFF1_OFF2:
            hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
            hw->ledctl_mode2 |= ledctl_off << (i << 3);
            break;
        default:
            /* Do nothing */
            break;
        }
    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Prepares SW controlable LED for use and saves the current state of the LED.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_setup_led(struct e1000_hw *hw)
{
    uint32_t ledctl;
    int32_t ret_val = E1000_SUCCESS;

    DEBUGFUNC("e1000_setup_led");

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
    case e1000_82543:
    case e1000_82544:
        /* No setup necessary */
        break;
    case e1000_82541:
    case e1000_82547:
    case e1000_82541_rev_2:
    case e1000_82547_rev_2:
        /* Turn off PHY Smart Power Down (if enabled) */
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_GMII_FIFO,
                                     &hw->phy_spd_default);
        if (ret_val)
            return ret_val;
        ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
                                      (uint16_t)(hw->phy_spd_default &
                                      ~IGP01E1000_GMII_SPD));
        if (ret_val)
            return ret_val;
        /* Fall Through */
    default:
        if (hw->media_type == e1000_media_type_fiber) {
            ledctl = E1000_READ_REG(hw, LEDCTL);
            /* Save current LEDCTL settings */
            hw->ledctl_default = ledctl;
            /* Turn off LED0 */
            ledctl &= ~(E1000_LEDCTL_LED0_IVRT |
                        E1000_LEDCTL_LED0_BLINK |
                        E1000_LEDCTL_LED0_MODE_MASK);
            ledctl |= (E1000_LEDCTL_MODE_LED_OFF <<
                       E1000_LEDCTL_LED0_MODE_SHIFT);
            E1000_WRITE_REG(hw, LEDCTL, ledctl);
        } else if (hw->media_type == e1000_media_type_copper)
            E1000_WRITE_REG(hw, LEDCTL, hw->ledctl_mode1);
        break;
    }

    return E1000_SUCCESS;
}


/******************************************************************************
 * Used on 82571 and later Si that has LED blink bits.
 * Callers must use their own timer and should have already called
 * e1000_id_led_init()
 * Call e1000_cleanup led() to stop blinking
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_blink_led_start(struct e1000_hw *hw)
{
    int16_t  i;
    uint32_t ledctl_blink = 0;

    DEBUGFUNC("e1000_id_led_blink_on");

    if (hw->mac_type < e1000_82571) {
        /* Nothing to do */
        return E1000_SUCCESS;
    }
    if (hw->media_type == e1000_media_type_fiber) {
        /* always blink LED0 for PCI-E fiber */
        ledctl_blink = E1000_LEDCTL_LED0_BLINK |
                     (E1000_LEDCTL_MODE_LED_ON << E1000_LEDCTL_LED0_MODE_SHIFT);
    } else {
        /* set the blink bit for each LED that's "on" (0x0E) in ledctl_mode2 */
        ledctl_blink = hw->ledctl_mode2;
        for (i=0; i < 4; i++)
            if (((hw->ledctl_mode2 >> (i * 8)) & 0xFF) ==
                E1000_LEDCTL_MODE_LED_ON)
                ledctl_blink |= (E1000_LEDCTL_LED0_BLINK << (i * 8));
    }

    E1000_WRITE_REG(hw, LEDCTL, ledctl_blink);

    return E1000_SUCCESS;
}

/******************************************************************************
 * Restores the saved state of the SW controlable LED.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_cleanup_led(struct e1000_hw *hw)
{
    int32_t ret_val = E1000_SUCCESS;

    DEBUGFUNC("e1000_cleanup_led");

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
    case e1000_82543:
    case e1000_82544:
        /* No cleanup necessary */
        break;
    case e1000_82541:
    case e1000_82547:
    case e1000_82541_rev_2:
    case e1000_82547_rev_2:
        /* Turn on PHY Smart Power Down (if previously enabled) */
        ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
                                      hw->phy_spd_default);
        if (ret_val)
            return ret_val;
        /* Fall Through */
    default:
        if (hw->phy_type == e1000_phy_ife) {
            e1000_write_phy_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED, 0);
            break;
        }
        /* Restore LEDCTL settings */
        E1000_WRITE_REG(hw, LEDCTL, hw->ledctl_default);
        break;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Turns on the software controllable LED
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_led_on(struct e1000_hw *hw)
{
    uint32_t ctrl = E1000_READ_REG(hw, CTRL);

    DEBUGFUNC("e1000_led_on");

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
    case e1000_82543:
        /* Set SW Defineable Pin 0 to turn on the LED */
        ctrl |= E1000_CTRL_SWDPIN0;
        ctrl |= E1000_CTRL_SWDPIO0;
        break;
    case e1000_82544:
        if (hw->media_type == e1000_media_type_fiber) {
            /* Set SW Defineable Pin 0 to turn on the LED */
            ctrl |= E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        } else {
            /* Clear SW Defineable Pin 0 to turn on the LED */
            ctrl &= ~E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        }
        break;
    default:
        if (hw->media_type == e1000_media_type_fiber) {
            /* Clear SW Defineable Pin 0 to turn on the LED */
            ctrl &= ~E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        } else if (hw->phy_type == e1000_phy_ife) {
            e1000_write_phy_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED,
                 (IFE_PSCL_PROBE_MODE | IFE_PSCL_PROBE_LEDS_ON));
        } else if (hw->media_type == e1000_media_type_copper) {
            E1000_WRITE_REG(hw, LEDCTL, hw->ledctl_mode2);
            return E1000_SUCCESS;
        }
        break;
    }

    E1000_WRITE_REG(hw, CTRL, ctrl);

    return E1000_SUCCESS;
}

/******************************************************************************
 * Turns off the software controllable LED
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
int32_t
e1000_led_off(struct e1000_hw *hw)
{
    uint32_t ctrl = E1000_READ_REG(hw, CTRL);

    DEBUGFUNC("e1000_led_off");

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
    case e1000_82543:
        /* Clear SW Defineable Pin 0 to turn off the LED */
        ctrl &= ~E1000_CTRL_SWDPIN0;
        ctrl |= E1000_CTRL_SWDPIO0;
        break;
    case e1000_82544:
        if (hw->media_type == e1000_media_type_fiber) {
            /* Clear SW Defineable Pin 0 to turn off the LED */
            ctrl &= ~E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        } else {
            /* Set SW Defineable Pin 0 to turn off the LED */
            ctrl |= E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        }
        break;
    default:
        if (hw->media_type == e1000_media_type_fiber) {
            /* Set SW Defineable Pin 0 to turn off the LED */
            ctrl |= E1000_CTRL_SWDPIN0;
            ctrl |= E1000_CTRL_SWDPIO0;
        } else if (hw->phy_type == e1000_phy_ife) {
            e1000_write_phy_reg(hw, IFE_PHY_SPECIAL_CONTROL_LED,
                 (IFE_PSCL_PROBE_MODE | IFE_PSCL_PROBE_LEDS_OFF));
        } else if (hw->media_type == e1000_media_type_copper) {
            E1000_WRITE_REG(hw, LEDCTL, hw->ledctl_mode1);
            return E1000_SUCCESS;
        }
        break;
    }

    E1000_WRITE_REG(hw, CTRL, ctrl);

    return E1000_SUCCESS;
}

/******************************************************************************
 * Clears all hardware statistics counters.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
e1000_clear_hw_cntrs(struct e1000_hw *hw)
{
    volatile uint32_t temp;

    temp = E1000_READ_REG(hw, CRCERRS);
    temp = E1000_READ_REG(hw, SYMERRS);
    temp = E1000_READ_REG(hw, MPC);
    temp = E1000_READ_REG(hw, SCC);
    temp = E1000_READ_REG(hw, ECOL);
    temp = E1000_READ_REG(hw, MCC);
    temp = E1000_READ_REG(hw, LATECOL);
    temp = E1000_READ_REG(hw, COLC);
    temp = E1000_READ_REG(hw, DC);
    temp = E1000_READ_REG(hw, SEC);
    temp = E1000_READ_REG(hw, RLEC);
    temp = E1000_READ_REG(hw, XONRXC);
    temp = E1000_READ_REG(hw, XONTXC);
    temp = E1000_READ_REG(hw, XOFFRXC);
    temp = E1000_READ_REG(hw, XOFFTXC);
    temp = E1000_READ_REG(hw, FCRUC);

    if (hw->mac_type != e1000_ich8lan) {
    temp = E1000_READ_REG(hw, PRC64);
    temp = E1000_READ_REG(hw, PRC127);
    temp = E1000_READ_REG(hw, PRC255);
    temp = E1000_READ_REG(hw, PRC511);
    temp = E1000_READ_REG(hw, PRC1023);
    temp = E1000_READ_REG(hw, PRC1522);
    }

    temp = E1000_READ_REG(hw, GPRC);
    temp = E1000_READ_REG(hw, BPRC);
    temp = E1000_READ_REG(hw, MPRC);
    temp = E1000_READ_REG(hw, GPTC);
    temp = E1000_READ_REG(hw, GORCL);
    temp = E1000_READ_REG(hw, GORCH);
    temp = E1000_READ_REG(hw, GOTCL);
    temp = E1000_READ_REG(hw, GOTCH);
    temp = E1000_READ_REG(hw, RNBC);
    temp = E1000_READ_REG(hw, RUC);
    temp = E1000_READ_REG(hw, RFC);
    temp = E1000_READ_REG(hw, ROC);
    temp = E1000_READ_REG(hw, RJC);
    temp = E1000_READ_REG(hw, TORL);
    temp = E1000_READ_REG(hw, TORH);
    temp = E1000_READ_REG(hw, TOTL);
    temp = E1000_READ_REG(hw, TOTH);
    temp = E1000_READ_REG(hw, TPR);
    temp = E1000_READ_REG(hw, TPT);

    if (hw->mac_type != e1000_ich8lan) {
    temp = E1000_READ_REG(hw, PTC64);
    temp = E1000_READ_REG(hw, PTC127);
    temp = E1000_READ_REG(hw, PTC255);
    temp = E1000_READ_REG(hw, PTC511);
    temp = E1000_READ_REG(hw, PTC1023);
    temp = E1000_READ_REG(hw, PTC1522);
    }

    temp = E1000_READ_REG(hw, MPTC);
    temp = E1000_READ_REG(hw, BPTC);

    if (hw->mac_type < e1000_82543) return;

    temp = E1000_READ_REG(hw, ALGNERRC);
    temp = E1000_READ_REG(hw, RXERRC);
    temp = E1000_READ_REG(hw, TNCRS);
    temp = E1000_READ_REG(hw, CEXTERR);
    temp = E1000_READ_REG(hw, TSCTC);
    temp = E1000_READ_REG(hw, TSCTFC);

    if (hw->mac_type <= e1000_82544) return;

    temp = E1000_READ_REG(hw, MGTPRC);
    temp = E1000_READ_REG(hw, MGTPDC);
    temp = E1000_READ_REG(hw, MGTPTC);

    if (hw->mac_type <= e1000_82547_rev_2) return;

    temp = E1000_READ_REG(hw, IAC);
    temp = E1000_READ_REG(hw, ICRXOC);

    if (hw->mac_type == e1000_ich8lan) return;

    temp = E1000_READ_REG(hw, ICRXPTC);
    temp = E1000_READ_REG(hw, ICRXATC);
    temp = E1000_READ_REG(hw, ICTXPTC);
    temp = E1000_READ_REG(hw, ICTXATC);
    temp = E1000_READ_REG(hw, ICTXQEC);
    temp = E1000_READ_REG(hw, ICTXQMTC);
    temp = E1000_READ_REG(hw, ICRXDMTC);
}

/******************************************************************************
 * Resets Adaptive IFS to its default state.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Call this after e1000_init_hw. You may override the IFS defaults by setting
 * hw->ifs_params_forced to TRUE. However, you must initialize hw->
 * current_ifs_val, ifs_min_val, ifs_max_val, ifs_step_size, and ifs_ratio
 * before calling this function.
 *****************************************************************************/
void
e1000_reset_adaptive(struct e1000_hw *hw)
{
    DEBUGFUNC("e1000_reset_adaptive");

    if (hw->adaptive_ifs) {
        if (!hw->ifs_params_forced) {
            hw->current_ifs_val = 0;
            hw->ifs_min_val = IFS_MIN;
            hw->ifs_max_val = IFS_MAX;
            hw->ifs_step_size = IFS_STEP;
            hw->ifs_ratio = IFS_RATIO;
        }
        hw->in_ifs_mode = FALSE;
        E1000_WRITE_REG(hw, AIT, 0);
    } else {
        DEBUGOUT("Not in Adaptive IFS mode!\n");
    }
}

/******************************************************************************
 * Called during the callback/watchdog routine to update IFS value based on
 * the ratio of transmits to collisions.
 *
 * hw - Struct containing variables accessed by shared code
 * tx_packets - Number of transmits since last callback
 * total_collisions - Number of collisions since last callback
 *****************************************************************************/
void
e1000_update_adaptive(struct e1000_hw *hw)
{
    DEBUGFUNC("e1000_update_adaptive");

    if (hw->adaptive_ifs) {
        if ((hw->collision_delta * hw->ifs_ratio) > hw->tx_packet_delta) {
            if (hw->tx_packet_delta > MIN_NUM_XMITS) {
                hw->in_ifs_mode = TRUE;
                if (hw->current_ifs_val < hw->ifs_max_val) {
                    if (hw->current_ifs_val == 0)
                        hw->current_ifs_val = hw->ifs_min_val;
                    else
                        hw->current_ifs_val += hw->ifs_step_size;
                    E1000_WRITE_REG(hw, AIT, hw->current_ifs_val);
                }
            }
        } else {
            if (hw->in_ifs_mode && (hw->tx_packet_delta <= MIN_NUM_XMITS)) {
                hw->current_ifs_val = 0;
                hw->in_ifs_mode = FALSE;
                E1000_WRITE_REG(hw, AIT, 0);
            }
        }
    } else {
        DEBUGOUT("Not in Adaptive IFS mode!\n");
    }
}

/******************************************************************************
 * Adjusts the statistic counters when a frame is accepted by TBI_ACCEPT
 *
 * hw - Struct containing variables accessed by shared code
 * frame_len - The length of the frame in question
 * mac_addr - The Ethernet destination address of the frame in question
 *****************************************************************************/
void
e1000_tbi_adjust_stats(struct e1000_hw *hw,
                       struct e1000_hw_stats *stats,
                       uint32_t frame_len,
                       uint8_t *mac_addr)
{
    uint64_t carry_bit;

    /* First adjust the frame length. */
    frame_len--;
    /* We need to adjust the statistics counters, since the hardware
     * counters overcount this packet as a CRC error and undercount
     * the packet as a good packet
     */
    /* This packet should not be counted as a CRC error.    */
    stats->crcerrs--;
    /* This packet does count as a Good Packet Received.    */
    stats->gprc++;

    /* Adjust the Good Octets received counters             */
    carry_bit = 0x80000000 & stats->gorcl;
    stats->gorcl += frame_len;
    /* If the high bit of Gorcl (the low 32 bits of the Good Octets
     * Received Count) was one before the addition,
     * AND it is zero after, then we lost the carry out,
     * need to add one to Gorch (Good Octets Received Count High).
     * This could be simplified if all environments supported
     * 64-bit integers.
     */
    if (carry_bit && ((stats->gorcl & 0x80000000) == 0))
        stats->gorch++;
    /* Is this a broadcast or multicast?  Check broadcast first,
     * since the test for a multicast frame will test positive on
     * a broadcast frame.
     */
    if ((mac_addr[0] == (uint8_t) 0xff) && (mac_addr[1] == (uint8_t) 0xff))
        /* Broadcast packet */
        stats->bprc++;
    else if (*mac_addr & 0x01)
        /* Multicast packet */
        stats->mprc++;

    if (frame_len == hw->max_frame_size) {
        /* In this case, the hardware has overcounted the number of
         * oversize frames.
         */
        if (stats->roc > 0)
            stats->roc--;
    }

    /* Adjust the bin counters when the extra byte put the frame in the
     * wrong bin. Remember that the frame_len was adjusted above.
     */
    if (frame_len == 64) {
        stats->prc64++;
        stats->prc127--;
    } else if (frame_len == 127) {
        stats->prc127++;
        stats->prc255--;
    } else if (frame_len == 255) {
        stats->prc255++;
        stats->prc511--;
    } else if (frame_len == 511) {
        stats->prc511++;
        stats->prc1023--;
    } else if (frame_len == 1023) {
        stats->prc1023++;
        stats->prc1522--;
    } else if (frame_len == 1522) {
        stats->prc1522++;
    }
}

/******************************************************************************
 * Gets the current PCI bus type, speed, and width of the hardware
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
e1000_get_bus_info(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t pci_ex_link_status;
    uint32_t status;

    switch (hw->mac_type) {
    case e1000_82542_rev2_0:
    case e1000_82542_rev2_1:
        hw->bus_type = e1000_bus_type_unknown;
        hw->bus_speed = e1000_bus_speed_unknown;
        hw->bus_width = e1000_bus_width_unknown;
        break;
    case e1000_82571:
    case e1000_82572:
    case e1000_82573:
    case e1000_80003es2lan:
        hw->bus_type = e1000_bus_type_pci_express;
        hw->bus_speed = e1000_bus_speed_2500;
        ret_val = e1000_read_pcie_cap_reg(hw,
                                      PCI_EX_LINK_STATUS,
                                      &pci_ex_link_status);
        if (ret_val)
            hw->bus_width = e1000_bus_width_unknown;
        else
            hw->bus_width = (pci_ex_link_status & PCI_EX_LINK_WIDTH_MASK) >>
                          PCI_EX_LINK_WIDTH_SHIFT;
        break;
    case e1000_ich8lan:
        hw->bus_type = e1000_bus_type_pci_express;
        hw->bus_speed = e1000_bus_speed_2500;
        hw->bus_width = e1000_bus_width_pciex_1;
        break;
    default:
        status = E1000_READ_REG(hw, STATUS);
        hw->bus_type = (status & E1000_STATUS_PCIX_MODE) ?
                       e1000_bus_type_pcix : e1000_bus_type_pci;

        if (hw->device_id == E1000_DEV_ID_82546EB_QUAD_COPPER) {
            hw->bus_speed = (hw->bus_type == e1000_bus_type_pci) ?
                            e1000_bus_speed_66 : e1000_bus_speed_120;
        } else if (hw->bus_type == e1000_bus_type_pci) {
            hw->bus_speed = (status & E1000_STATUS_PCI66) ?
                            e1000_bus_speed_66 : e1000_bus_speed_33;
        } else {
            switch (status & E1000_STATUS_PCIX_SPEED) {
            case E1000_STATUS_PCIX_SPEED_66:
                hw->bus_speed = e1000_bus_speed_66;
                break;
            case E1000_STATUS_PCIX_SPEED_100:
                hw->bus_speed = e1000_bus_speed_100;
                break;
            case E1000_STATUS_PCIX_SPEED_133:
                hw->bus_speed = e1000_bus_speed_133;
                break;
            default:
                hw->bus_speed = e1000_bus_speed_reserved;
                break;
            }
        }
        hw->bus_width = (status & E1000_STATUS_BUS64) ?
                        e1000_bus_width_64 : e1000_bus_width_32;
        break;
    }
}

/******************************************************************************
 * Writes a value to one of the devices registers using port I/O (as opposed to
 * memory mapped I/O). Only 82544 and newer devices support port I/O.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset to write to
 * value - value to write
 *****************************************************************************/
static void
e1000_write_reg_io(struct e1000_hw *hw,
                   uint32_t offset,
                   uint32_t value)
{
    unsigned long io_addr = hw->io_base;
    unsigned long io_data = hw->io_base + 4;

    e1000_io_write(hw, io_addr, offset);
    e1000_io_write(hw, io_data, value);
}

/******************************************************************************
 * Estimates the cable length.
 *
 * hw - Struct containing variables accessed by shared code
 * min_length - The estimated minimum length
 * max_length - The estimated maximum length
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * This function always returns a ranged length (minimum & maximum).
 * So for M88 phy's, this function interprets the one value returned from the
 * register to the minimum and maximum range.
 * For IGP phy's, the function calculates the range by the AGC registers.
 *****************************************************************************/
static int32_t
e1000_get_cable_length(struct e1000_hw *hw,
                       uint16_t *min_length,
                       uint16_t *max_length)
{
    int32_t ret_val;
    uint16_t agc_value = 0;
    uint16_t i, phy_data;
    uint16_t cable_length;

    DEBUGFUNC("e1000_get_cable_length");

    *min_length = *max_length = 0;

    /* Use old method for Phy older than IGP */
    if (hw->phy_type == e1000_phy_m88) {

        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
                                     &phy_data);
        if (ret_val)
            return ret_val;
        cable_length = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
                       M88E1000_PSSR_CABLE_LENGTH_SHIFT;

        /* Convert the enum value to ranged values */
        switch (cable_length) {
        case e1000_cable_length_50:
            *min_length = 0;
            *max_length = e1000_igp_cable_length_50;
            break;
        case e1000_cable_length_50_80:
            *min_length = e1000_igp_cable_length_50;
            *max_length = e1000_igp_cable_length_80;
            break;
        case e1000_cable_length_80_110:
            *min_length = e1000_igp_cable_length_80;
            *max_length = e1000_igp_cable_length_110;
            break;
        case e1000_cable_length_110_140:
            *min_length = e1000_igp_cable_length_110;
            *max_length = e1000_igp_cable_length_140;
            break;
        case e1000_cable_length_140:
            *min_length = e1000_igp_cable_length_140;
            *max_length = e1000_igp_cable_length_170;
            break;
        default:
            return -E1000_ERR_PHY;
            break;
        }
    } else if (hw->phy_type == e1000_phy_gg82563) {
        ret_val = e1000_read_phy_reg(hw, GG82563_PHY_DSP_DISTANCE,
                                     &phy_data);
        if (ret_val)
            return ret_val;
        cable_length = phy_data & GG82563_DSPD_CABLE_LENGTH;

        switch (cable_length) {
        case e1000_gg_cable_length_60:
            *min_length = 0;
            *max_length = e1000_igp_cable_length_60;
            break;
        case e1000_gg_cable_length_60_115:
            *min_length = e1000_igp_cable_length_60;
            *max_length = e1000_igp_cable_length_115;
            break;
        case e1000_gg_cable_length_115_150:
            *min_length = e1000_igp_cable_length_115;
            *max_length = e1000_igp_cable_length_150;
            break;
        case e1000_gg_cable_length_150:
            *min_length = e1000_igp_cable_length_150;
            *max_length = e1000_igp_cable_length_180;
            break;
        default:
            return -E1000_ERR_PHY;
            break;
        }
    } else if (hw->phy_type == e1000_phy_igp) { /* For IGP PHY */
        uint16_t cur_agc_value;
        uint16_t min_agc_value = IGP01E1000_AGC_LENGTH_TABLE_SIZE;
        uint16_t agc_reg_array[IGP01E1000_PHY_CHANNEL_NUM] =
                                                         {IGP01E1000_PHY_AGC_A,
                                                          IGP01E1000_PHY_AGC_B,
                                                          IGP01E1000_PHY_AGC_C,
                                                          IGP01E1000_PHY_AGC_D};
        /* Read the AGC registers for all channels */
        for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {

            ret_val = e1000_read_phy_reg(hw, agc_reg_array[i], &phy_data);
            if (ret_val)
                return ret_val;

            cur_agc_value = phy_data >> IGP01E1000_AGC_LENGTH_SHIFT;

            /* Value bound check. */
            if ((cur_agc_value >= IGP01E1000_AGC_LENGTH_TABLE_SIZE - 1) ||
                (cur_agc_value == 0))
                return -E1000_ERR_PHY;

            agc_value += cur_agc_value;

            /* Update minimal AGC value. */
            if (min_agc_value > cur_agc_value)
                min_agc_value = cur_agc_value;
        }

        /* Remove the minimal AGC result for length < 50m */
        if (agc_value < IGP01E1000_PHY_CHANNEL_NUM * e1000_igp_cable_length_50) {
            agc_value -= min_agc_value;

            /* Get the average length of the remaining 3 channels */
            agc_value /= (IGP01E1000_PHY_CHANNEL_NUM - 1);
        } else {
            /* Get the average length of all the 4 channels. */
            agc_value /= IGP01E1000_PHY_CHANNEL_NUM;
        }

        /* Set the range of the calculated length. */
        *min_length = ((e1000_igp_cable_length_table[agc_value] -
                       IGP01E1000_AGC_RANGE) > 0) ?
                       (e1000_igp_cable_length_table[agc_value] -
                       IGP01E1000_AGC_RANGE) : 0;
        *max_length = e1000_igp_cable_length_table[agc_value] +
                      IGP01E1000_AGC_RANGE;
    } else if (hw->phy_type == e1000_phy_igp_2 ||
               hw->phy_type == e1000_phy_igp_3) {
        uint16_t cur_agc_index, max_agc_index = 0;
        uint16_t min_agc_index = IGP02E1000_AGC_LENGTH_TABLE_SIZE - 1;
        uint16_t agc_reg_array[IGP02E1000_PHY_CHANNEL_NUM] =
                                                         {IGP02E1000_PHY_AGC_A,
                                                          IGP02E1000_PHY_AGC_B,
                                                          IGP02E1000_PHY_AGC_C,
                                                          IGP02E1000_PHY_AGC_D};
        /* Read the AGC registers for all channels */
        for (i = 0; i < IGP02E1000_PHY_CHANNEL_NUM; i++) {
            ret_val = e1000_read_phy_reg(hw, agc_reg_array[i], &phy_data);
            if (ret_val)
                return ret_val;

            /* Getting bits 15:9, which represent the combination of course and
             * fine gain values.  The result is a number that can be put into
             * the lookup table to obtain the approximate cable length. */
            cur_agc_index = (phy_data >> IGP02E1000_AGC_LENGTH_SHIFT) &
                            IGP02E1000_AGC_LENGTH_MASK;

            /* Array index bound check. */
            if ((cur_agc_index >= IGP02E1000_AGC_LENGTH_TABLE_SIZE) ||
                (cur_agc_index == 0))
                return -E1000_ERR_PHY;

            /* Remove min & max AGC values from calculation. */
            if (e1000_igp_2_cable_length_table[min_agc_index] >
                e1000_igp_2_cable_length_table[cur_agc_index])
                min_agc_index = cur_agc_index;
            if (e1000_igp_2_cable_length_table[max_agc_index] <
                e1000_igp_2_cable_length_table[cur_agc_index])
                max_agc_index = cur_agc_index;

            agc_value += e1000_igp_2_cable_length_table[cur_agc_index];
        }

        agc_value -= (e1000_igp_2_cable_length_table[min_agc_index] +
                      e1000_igp_2_cable_length_table[max_agc_index]);
        agc_value /= (IGP02E1000_PHY_CHANNEL_NUM - 2);

        /* Calculate cable length with the error range of +/- 10 meters. */
        *min_length = ((agc_value - IGP02E1000_AGC_RANGE) > 0) ?
                       (agc_value - IGP02E1000_AGC_RANGE) : 0;
        *max_length = agc_value + IGP02E1000_AGC_RANGE;
    }

    return E1000_SUCCESS;
}

/******************************************************************************
 * Check the cable polarity
 *
 * hw - Struct containing variables accessed by shared code
 * polarity - output parameter : 0 - Polarity is not reversed
 *                               1 - Polarity is reversed.
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * For phy's older then IGP, this function simply reads the polarity bit in the
 * Phy Status register.  For IGP phy's, this bit is valid only if link speed is
 * 10 Mbps.  If the link speed is 100 Mbps there is no polarity so this bit will
 * return 0.  If the link speed is 1000 Mbps the polarity status is in the
 * IGP01E1000_PHY_PCS_INIT_REG.
 *****************************************************************************/
static int32_t
e1000_check_polarity(struct e1000_hw *hw,
                     e1000_rev_polarity *polarity)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_check_polarity");

    if ((hw->phy_type == e1000_phy_m88) ||
        (hw->phy_type == e1000_phy_gg82563)) {
        /* return the Polarity bit in the Status register. */
        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
                                     &phy_data);
        if (ret_val)
            return ret_val;
        *polarity = ((phy_data & M88E1000_PSSR_REV_POLARITY) >>
                     M88E1000_PSSR_REV_POLARITY_SHIFT) ?
                     e1000_rev_polarity_reversed : e1000_rev_polarity_normal;

    } else if (hw->phy_type == e1000_phy_igp ||
              hw->phy_type == e1000_phy_igp_3 ||
              hw->phy_type == e1000_phy_igp_2) {
        /* Read the Status register to check the speed */
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_STATUS,
                                     &phy_data);
        if (ret_val)
            return ret_val;

        /* If speed is 1000 Mbps, must read the IGP01E1000_PHY_PCS_INIT_REG to
         * find the polarity status */
        if ((phy_data & IGP01E1000_PSSR_SPEED_MASK) ==
           IGP01E1000_PSSR_SPEED_1000MBPS) {

            /* Read the GIG initialization PCS register (0x00B4) */
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PCS_INIT_REG,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            /* Check the polarity bits */
            *polarity = (phy_data & IGP01E1000_PHY_POLARITY_MASK) ?
                         e1000_rev_polarity_reversed : e1000_rev_polarity_normal;
        } else {
            /* For 10 Mbps, read the polarity bit in the status register. (for
             * 100 Mbps this bit is always 0) */
            *polarity = (phy_data & IGP01E1000_PSSR_POLARITY_REVERSED) ?
                         e1000_rev_polarity_reversed : e1000_rev_polarity_normal;
        }
    } else if (hw->phy_type == e1000_phy_ife) {
        ret_val = e1000_read_phy_reg(hw, IFE_PHY_EXTENDED_STATUS_CONTROL,
                                     &phy_data);
        if (ret_val)
            return ret_val;
        *polarity = ((phy_data & IFE_PESC_POLARITY_REVERSED) >>
                     IFE_PESC_POLARITY_REVERSED_SHIFT) ?
                     e1000_rev_polarity_reversed : e1000_rev_polarity_normal;
    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Check if Downshift occured
 *
 * hw - Struct containing variables accessed by shared code
 * downshift - output parameter : 0 - No Downshift ocured.
 *                                1 - Downshift ocured.
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * For phy's older then IGP, this function reads the Downshift bit in the Phy
 * Specific Status register.  For IGP phy's, it reads the Downgrade bit in the
 * Link Health register.  In IGP this bit is latched high, so the driver must
 * read it immediately after link is established.
 *****************************************************************************/
static int32_t
e1000_check_downshift(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t phy_data;

    DEBUGFUNC("e1000_check_downshift");

    if (hw->phy_type == e1000_phy_igp ||
        hw->phy_type == e1000_phy_igp_3 ||
        hw->phy_type == e1000_phy_igp_2) {
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_LINK_HEALTH,
                                     &phy_data);
        if (ret_val)
            return ret_val;

        hw->speed_downgraded = (phy_data & IGP01E1000_PLHR_SS_DOWNGRADE) ? 1 : 0;
    } else if ((hw->phy_type == e1000_phy_m88) ||
               (hw->phy_type == e1000_phy_gg82563)) {
        ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
                                     &phy_data);
        if (ret_val)
            return ret_val;

        hw->speed_downgraded = (phy_data & M88E1000_PSSR_DOWNSHIFT) >>
                               M88E1000_PSSR_DOWNSHIFT_SHIFT;
    } else if (hw->phy_type == e1000_phy_ife) {
        /* e1000_phy_ife supports 10/100 speed only */
        hw->speed_downgraded = FALSE;
    }

    return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * 82541_rev_2 & 82547_rev_2 have the capability to configure the DSP when a
 * gigabit link is achieved to improve link quality.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/

static int32_t
e1000_config_dsp_after_link_change(struct e1000_hw *hw,
                                   boolean_t link_up)
{
    int32_t ret_val;
    uint16_t phy_data, phy_saved_data, speed, duplex, i;
    uint16_t dsp_reg_array[IGP01E1000_PHY_CHANNEL_NUM] =
                                        {IGP01E1000_PHY_AGC_PARAM_A,
                                        IGP01E1000_PHY_AGC_PARAM_B,
                                        IGP01E1000_PHY_AGC_PARAM_C,
                                        IGP01E1000_PHY_AGC_PARAM_D};
    uint16_t min_length, max_length;

    DEBUGFUNC("e1000_config_dsp_after_link_change");

    if (hw->phy_type != e1000_phy_igp)
        return E1000_SUCCESS;

    if (link_up) {
        ret_val = e1000_get_speed_and_duplex(hw, &speed, &duplex);
        if (ret_val) {
            DEBUGOUT("Error getting link speed and duplex\n");
            return ret_val;
        }

        if (speed == SPEED_1000) {

            ret_val = e1000_get_cable_length(hw, &min_length, &max_length);
            if (ret_val)
                return ret_val;

            if ((hw->dsp_config_state == e1000_dsp_config_enabled) &&
                min_length >= e1000_igp_cable_length_50) {

                for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
                    ret_val = e1000_read_phy_reg(hw, dsp_reg_array[i],
                                                 &phy_data);
                    if (ret_val)
                        return ret_val;

                    phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;

                    ret_val = e1000_write_phy_reg(hw, dsp_reg_array[i],
                                                  phy_data);
                    if (ret_val)
                        return ret_val;
                }
                hw->dsp_config_state = e1000_dsp_config_activated;
            }

            if ((hw->ffe_config_state == e1000_ffe_config_enabled) &&
               (min_length < e1000_igp_cable_length_50)) {

                uint16_t ffe_idle_err_timeout = FFE_IDLE_ERR_COUNT_TIMEOUT_20;
                uint32_t idle_errs = 0;

                /* clear previous idle error counts */
                ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS,
                                             &phy_data);
                if (ret_val)
                    return ret_val;

                for (i = 0; i < ffe_idle_err_timeout; i++) {
                    udelay(1000);
                    ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS,
                                                 &phy_data);
                    if (ret_val)
                        return ret_val;

                    idle_errs += (phy_data & SR_1000T_IDLE_ERROR_CNT);
                    if (idle_errs > SR_1000T_PHY_EXCESSIVE_IDLE_ERR_COUNT) {
                        hw->ffe_config_state = e1000_ffe_config_active;

                        ret_val = e1000_write_phy_reg(hw,
                                    IGP01E1000_PHY_DSP_FFE,
                                    IGP01E1000_PHY_DSP_FFE_CM_CP);
                        if (ret_val)
                            return ret_val;
                        break;
                    }

                    if (idle_errs)
                        ffe_idle_err_timeout = FFE_IDLE_ERR_COUNT_TIMEOUT_100;
                }
            }
        }
    } else {
        if (hw->dsp_config_state == e1000_dsp_config_activated) {
            /* Save off the current value of register 0x2F5B to be restored at
             * the end of the routines. */
            ret_val = e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

            if (ret_val)
                return ret_val;

            /* Disable the PHY transmitter */
            ret_val = e1000_write_phy_reg(hw, 0x2F5B, 0x0003);

            if (ret_val)
                return ret_val;

            mdelay(20);

            ret_val = e1000_write_phy_reg(hw, 0x0000,
                                          IGP01E1000_IEEE_FORCE_GIGA);
            if (ret_val)
                return ret_val;
            for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
                ret_val = e1000_read_phy_reg(hw, dsp_reg_array[i], &phy_data);
                if (ret_val)
                    return ret_val;

                phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;
                phy_data |=  IGP01E1000_PHY_EDAC_SIGN_EXT_9_BITS;

                ret_val = e1000_write_phy_reg(hw,dsp_reg_array[i], phy_data);
                if (ret_val)
                    return ret_val;
            }

            ret_val = e1000_write_phy_reg(hw, 0x0000,
                                          IGP01E1000_IEEE_RESTART_AUTONEG);
            if (ret_val)
                return ret_val;

            mdelay(20);

            /* Now enable the transmitter */
            ret_val = e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

            if (ret_val)
                return ret_val;

            hw->dsp_config_state = e1000_dsp_config_enabled;
        }

        if (hw->ffe_config_state == e1000_ffe_config_active) {
            /* Save off the current value of register 0x2F5B to be restored at
             * the end of the routines. */
            ret_val = e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

            if (ret_val)
                return ret_val;

            /* Disable the PHY transmitter */
            ret_val = e1000_write_phy_reg(hw, 0x2F5B, 0x0003);

            if (ret_val)
                return ret_val;

            mdelay(20);

            ret_val = e1000_write_phy_reg(hw, 0x0000,
                                          IGP01E1000_IEEE_FORCE_GIGA);
            if (ret_val)
                return ret_val;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_DSP_FFE,
                                          IGP01E1000_PHY_DSP_FFE_DEFAULT);
            if (ret_val)
                return ret_val;

            ret_val = e1000_write_phy_reg(hw, 0x0000,
                                          IGP01E1000_IEEE_RESTART_AUTONEG);
            if (ret_val)
                return ret_val;

            mdelay(20);

            /* Now enable the transmitter */
            ret_val = e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

            if (ret_val)
                return ret_val;

            hw->ffe_config_state = e1000_ffe_config_enabled;
        }
    }
    return E1000_SUCCESS;
}

/*****************************************************************************
 * Set PHY to class A mode
 * Assumes the following operations will follow to enable the new class mode.
 *  1. Do a PHY soft reset
 *  2. Restart auto-negotiation or force link.
 *
 * hw - Struct containing variables accessed by shared code
 ****************************************************************************/
static int32_t
e1000_set_phy_mode(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t eeprom_data;

    DEBUGFUNC("e1000_set_phy_mode");

    if ((hw->mac_type == e1000_82545_rev_3) &&
        (hw->media_type == e1000_media_type_copper)) {
        ret_val = e1000_read_eeprom(hw, EEPROM_PHY_CLASS_WORD, 1, &eeprom_data);
        if (ret_val) {
            return ret_val;
        }

        if ((eeprom_data != EEPROM_RESERVED_WORD) &&
            (eeprom_data & EEPROM_PHY_CLASS_A)) {
            ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x000B);
            if (ret_val)
                return ret_val;
            ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0x8104);
            if (ret_val)
                return ret_val;

            hw->phy_reset_disable = FALSE;
        }
    }

    return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * This function sets the lplu state according to the active flag.  When
 * activating lplu this function also disables smart speed and vise versa.
 * lplu will not be activated unless the device autonegotiation advertisment
 * meets standards of either 10 or 10/100 or 10/100/1000 at all duplexes.
 * hw: Struct containing variables accessed by shared code
 * active - true to enable lplu false to disable lplu.
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/

static int32_t
e1000_set_d3_lplu_state(struct e1000_hw *hw,
                        boolean_t active)
{
    uint32_t phy_ctrl = 0;
    int32_t ret_val;
    uint16_t phy_data;
    DEBUGFUNC("e1000_set_d3_lplu_state");

    if (hw->phy_type != e1000_phy_igp && hw->phy_type != e1000_phy_igp_2
        && hw->phy_type != e1000_phy_igp_3)
        return E1000_SUCCESS;

    /* During driver activity LPLU should not be used or it will attain link
     * from the lowest speeds starting from 10Mbps. The capability is used for
     * Dx transitions and states */
    if (hw->mac_type == e1000_82541_rev_2 || hw->mac_type == e1000_82547_rev_2) {
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_GMII_FIFO, &phy_data);
        if (ret_val)
            return ret_val;
    } else if (hw->mac_type == e1000_ich8lan) {
        /* MAC writes into PHY register based on the state transition
         * and start auto-negotiation. SW driver can overwrite the settings
         * in CSR PHY power control E1000_PHY_CTRL register. */
        phy_ctrl = E1000_READ_REG(hw, PHY_CTRL);
    } else {
        ret_val = e1000_read_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT, &phy_data);
        if (ret_val)
            return ret_val;
    }

    if (!active) {
        if (hw->mac_type == e1000_82541_rev_2 ||
            hw->mac_type == e1000_82547_rev_2) {
            phy_data &= ~IGP01E1000_GMII_FLEX_SPD;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO, phy_data);
            if (ret_val)
                return ret_val;
        } else {
            if (hw->mac_type == e1000_ich8lan) {
                phy_ctrl &= ~E1000_PHY_CTRL_NOND0A_LPLU;
                E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
            } else {
                phy_data &= ~IGP02E1000_PM_D3_LPLU;
                ret_val = e1000_write_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT,
                                              phy_data);
                if (ret_val)
                    return ret_val;
            }
        }

        /* LPLU and SmartSpeed are mutually exclusive.  LPLU is used during
         * Dx states where the power conservation is most important.  During
         * driver activity we should enable SmartSpeed, so performance is
         * maintained. */
        if (hw->smart_speed == e1000_smart_speed_on) {
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_data |= IGP01E1000_PSCFR_SMART_SPEED;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                          phy_data);
            if (ret_val)
                return ret_val;
        } else if (hw->smart_speed == e1000_smart_speed_off) {
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                          phy_data);
            if (ret_val)
                return ret_val;
        }

    } else if ((hw->autoneg_advertised == AUTONEG_ADVERTISE_SPEED_DEFAULT) ||
               (hw->autoneg_advertised == AUTONEG_ADVERTISE_10_ALL ) ||
               (hw->autoneg_advertised == AUTONEG_ADVERTISE_10_100_ALL)) {

        if (hw->mac_type == e1000_82541_rev_2 ||
            hw->mac_type == e1000_82547_rev_2) {
            phy_data |= IGP01E1000_GMII_FLEX_SPD;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO, phy_data);
            if (ret_val)
                return ret_val;
        } else {
            if (hw->mac_type == e1000_ich8lan) {
                phy_ctrl |= E1000_PHY_CTRL_NOND0A_LPLU;
                E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
            } else {
                phy_data |= IGP02E1000_PM_D3_LPLU;
                ret_val = e1000_write_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT,
                                              phy_data);
                if (ret_val)
                    return ret_val;
            }
        }

        /* When LPLU is enabled we should disable SmartSpeed */
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
        ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG, phy_data);
        if (ret_val)
            return ret_val;

    }
    return E1000_SUCCESS;
}

/*****************************************************************************
 *
 * This function sets the lplu d0 state according to the active flag.  When
 * activating lplu this function also disables smart speed and vise versa.
 * lplu will not be activated unless the device autonegotiation advertisment
 * meets standards of either 10 or 10/100 or 10/100/1000 at all duplexes.
 * hw: Struct containing variables accessed by shared code
 * active - true to enable lplu false to disable lplu.
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 ****************************************************************************/

static int32_t
e1000_set_d0_lplu_state(struct e1000_hw *hw,
                        boolean_t active)
{
    uint32_t phy_ctrl = 0;
    int32_t ret_val;
    uint16_t phy_data;
    DEBUGFUNC("e1000_set_d0_lplu_state");

    if (hw->mac_type <= e1000_82547_rev_2)
        return E1000_SUCCESS;

    if (hw->mac_type == e1000_ich8lan) {
        phy_ctrl = E1000_READ_REG(hw, PHY_CTRL);
    } else {
        ret_val = e1000_read_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT, &phy_data);
        if (ret_val)
            return ret_val;
    }

    if (!active) {
        if (hw->mac_type == e1000_ich8lan) {
            phy_ctrl &= ~E1000_PHY_CTRL_D0A_LPLU;
            E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
        } else {
            phy_data &= ~IGP02E1000_PM_D0_LPLU;
            ret_val = e1000_write_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT, phy_data);
            if (ret_val)
                return ret_val;
        }

        /* LPLU and SmartSpeed are mutually exclusive.  LPLU is used during
         * Dx states where the power conservation is most important.  During
         * driver activity we should enable SmartSpeed, so performance is
         * maintained. */
        if (hw->smart_speed == e1000_smart_speed_on) {
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_data |= IGP01E1000_PSCFR_SMART_SPEED;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                          phy_data);
            if (ret_val)
                return ret_val;
        } else if (hw->smart_speed == e1000_smart_speed_off) {
            ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                         &phy_data);
            if (ret_val)
                return ret_val;

            phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
            ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
                                          phy_data);
            if (ret_val)
                return ret_val;
        }


    } else {

        if (hw->mac_type == e1000_ich8lan) {
            phy_ctrl |= E1000_PHY_CTRL_D0A_LPLU;
            E1000_WRITE_REG(hw, PHY_CTRL, phy_ctrl);
        } else {
            phy_data |= IGP02E1000_PM_D0_LPLU;
            ret_val = e1000_write_phy_reg(hw, IGP02E1000_PHY_POWER_MGMT, phy_data);
            if (ret_val)
                return ret_val;
        }

        /* When LPLU is enabled we should disable SmartSpeed */
        ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG, &phy_data);
        if (ret_val)
            return ret_val;

        phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
        ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG, phy_data);
        if (ret_val)
            return ret_val;

    }
    return E1000_SUCCESS;
}

/******************************************************************************
 * Change VCO speed register to improve Bit Error Rate performance of SERDES.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
e1000_set_vco_speed(struct e1000_hw *hw)
{
    int32_t  ret_val;
    uint16_t default_page = 0;
    uint16_t phy_data;

    DEBUGFUNC("e1000_set_vco_speed");

    switch (hw->mac_type) {
    case e1000_82545_rev_3:
    case e1000_82546_rev_3:
       break;
    default:
        return E1000_SUCCESS;
    }

    /* Set PHY register 30, page 5, bit 8 to 0 */

    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, &default_page);
    if (ret_val)
        return ret_val;

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0005);
    if (ret_val)
        return ret_val;

    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_data &= ~M88E1000_PHY_VCO_REG_BIT8;
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
    if (ret_val)
        return ret_val;

    /* Set PHY register 30, page 4, bit 11 to 1 */

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0004);
    if (ret_val)
        return ret_val;

    ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
    if (ret_val)
        return ret_val;

    phy_data |= M88E1000_PHY_VCO_REG_BIT11;
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
    if (ret_val)
        return ret_val;

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, default_page);
    if (ret_val)
        return ret_val;

    return E1000_SUCCESS;
}


/*****************************************************************************
 * This function reads the cookie from ARC ram.
 *
 * returns: - E1000_SUCCESS .
 ****************************************************************************/
static int32_t
e1000_host_if_read_cookie(struct e1000_hw * hw, uint8_t *buffer)
{
    uint8_t i;
    uint32_t offset = E1000_MNG_DHCP_COOKIE_OFFSET;
    uint8_t length = E1000_MNG_DHCP_COOKIE_LENGTH;

    length = (length >> 2);
    offset = (offset >> 2);

    for (i = 0; i < length; i++) {
        *((uint32_t *) buffer + i) =
            E1000_READ_REG_ARRAY_DWORD(hw, HOST_IF, offset + i);
    }
    return E1000_SUCCESS;
}


/*****************************************************************************
 * This function checks whether the HOST IF is enabled for command operaton
 * and also checks whether the previous command is completed.
 * It busy waits in case of previous command is not completed.
 *
 * returns: - E1000_ERR_HOST_INTERFACE_COMMAND in case if is not ready or
 *            timeout
 *          - E1000_SUCCESS for success.
 ****************************************************************************/
static int32_t
e1000_mng_enable_host_if(struct e1000_hw * hw)
{
    uint32_t hicr;
    uint8_t i;

    /* Check that the host interface is enabled. */
    hicr = E1000_READ_REG(hw, HICR);
    if ((hicr & E1000_HICR_EN) == 0) {
        DEBUGOUT("E1000_HOST_EN bit disabled.\n");
        return -E1000_ERR_HOST_INTERFACE_COMMAND;
    }
    /* check the previous command is completed */
    for (i = 0; i < E1000_MNG_DHCP_COMMAND_TIMEOUT; i++) {
        hicr = E1000_READ_REG(hw, HICR);
        if (!(hicr & E1000_HICR_C))
            break;
        mdelay(1);
    }

    if (i == E1000_MNG_DHCP_COMMAND_TIMEOUT) {
        DEBUGOUT("Previous command timeout failed .\n");
        return -E1000_ERR_HOST_INTERFACE_COMMAND;
    }
    return E1000_SUCCESS;
}

/*****************************************************************************
 * This function writes the buffer content at the offset given on the host if.
 * It also does alignment considerations to do the writes in most efficient way.
 * Also fills up the sum of the buffer in *buffer parameter.
 *
 * returns  - E1000_SUCCESS for success.
 ****************************************************************************/
static int32_t
e1000_mng_host_if_write(struct e1000_hw * hw, uint8_t *buffer,
                        uint16_t length, uint16_t offset, uint8_t *sum)
{
    uint8_t *tmp;
    uint8_t *bufptr = buffer;
    uint32_t data = 0;
    uint16_t remaining, i, j, prev_bytes;

    /* sum = only sum of the data and it is not checksum */

    if (length == 0 || offset + length > E1000_HI_MAX_MNG_DATA_LENGTH) {
        return -E1000_ERR_PARAM;
    }

    tmp = (uint8_t *)&data;
    prev_bytes = offset & 0x3;
    offset &= 0xFFFC;
    offset >>= 2;

    if (prev_bytes) {
        data = E1000_READ_REG_ARRAY_DWORD(hw, HOST_IF, offset);
        for (j = prev_bytes; j < sizeof(uint32_t); j++) {
            *(tmp + j) = *bufptr++;
            *sum += *(tmp + j);
        }
        E1000_WRITE_REG_ARRAY_DWORD(hw, HOST_IF, offset, data);
        length -= j - prev_bytes;
        offset++;
    }

    remaining = length & 0x3;
    length -= remaining;

    /* Calculate length in DWORDs */
    length >>= 2;

    /* The device driver writes the relevant command block into the
     * ram area. */
    for (i = 0; i < length; i++) {
        for (j = 0; j < sizeof(uint32_t); j++) {
            *(tmp + j) = *bufptr++;
            *sum += *(tmp + j);
        }

        E1000_WRITE_REG_ARRAY_DWORD(hw, HOST_IF, offset + i, data);
    }
    if (remaining) {
        for (j = 0; j < sizeof(uint32_t); j++) {
            if (j < remaining)
                *(tmp + j) = *bufptr++;
            else
                *(tmp + j) = 0;

            *sum += *(tmp + j);
        }
        E1000_WRITE_REG_ARRAY_DWORD(hw, HOST_IF, offset + i, data);
    }

    return E1000_SUCCESS;
}


/*****************************************************************************
 * This function writes the command header after does the checksum calculation.
 *
 * returns  - E1000_SUCCESS for success.
 ****************************************************************************/
static int32_t
e1000_mng_write_cmd_header(struct e1000_hw * hw,
                           struct e1000_host_mng_command_header * hdr)
{
    uint16_t i;
    uint8_t sum;
    uint8_t *buffer;

    /* Write the whole command header structure which includes sum of
     * the buffer */

    uint16_t length = sizeof(struct e1000_host_mng_command_header);

    sum = hdr->checksum;
    hdr->checksum = 0;

    buffer = (uint8_t *) hdr;
    i = length;
    while (i--)
        sum += buffer[i];

    hdr->checksum = 0 - sum;

    length >>= 2;
    /* The device driver writes the relevant command block into the ram area. */
    for (i = 0; i < length; i++) {
        E1000_WRITE_REG_ARRAY_DWORD(hw, HOST_IF, i, *((uint32_t *) hdr + i));
        E1000_WRITE_FLUSH(hw);
    }

    return E1000_SUCCESS;
}


/*****************************************************************************
 * This function indicates to ARC that a new command is pending which completes
 * one write operation by the driver.
 *
 * returns  - E1000_SUCCESS for success.
 ****************************************************************************/
static int32_t
e1000_mng_write_commit(struct e1000_hw * hw)
{
    uint32_t hicr;

    hicr = E1000_READ_REG(hw, HICR);
    /* Setting this bit tells the ARC that a new command is pending. */
    E1000_WRITE_REG(hw, HICR, hicr | E1000_HICR_C);

    return E1000_SUCCESS;
}


/*****************************************************************************
 * This function checks the mode of the firmware.
 *
 * returns  - TRUE when the mode is IAMT or FALSE.
 ****************************************************************************/
boolean_t
e1000_check_mng_mode(struct e1000_hw *hw)
{
    uint32_t fwsm;

    fwsm = E1000_READ_REG(hw, FWSM);

    if (hw->mac_type == e1000_ich8lan) {
        if ((fwsm & E1000_FWSM_MODE_MASK) ==
            (E1000_MNG_ICH_IAMT_MODE << E1000_FWSM_MODE_SHIFT))
            return TRUE;
    } else if ((fwsm & E1000_FWSM_MODE_MASK) ==
               (E1000_MNG_IAMT_MODE << E1000_FWSM_MODE_SHIFT))
        return TRUE;

    return FALSE;
}


/*****************************************************************************
 * This function writes the dhcp info .
 ****************************************************************************/
int32_t
e1000_mng_write_dhcp_info(struct e1000_hw * hw, uint8_t *buffer,
                          uint16_t length)
{
    int32_t ret_val;
    struct e1000_host_mng_command_header hdr;

    hdr.command_id = E1000_MNG_DHCP_TX_PAYLOAD_CMD;
    hdr.command_length = length;
    hdr.reserved1 = 0;
    hdr.reserved2 = 0;
    hdr.checksum = 0;

    ret_val = e1000_mng_enable_host_if(hw);
    if (ret_val == E1000_SUCCESS) {
        ret_val = e1000_mng_host_if_write(hw, buffer, length, sizeof(hdr),
                                          &(hdr.checksum));
        if (ret_val == E1000_SUCCESS) {
            ret_val = e1000_mng_write_cmd_header(hw, &hdr);
            if (ret_val == E1000_SUCCESS)
                ret_val = e1000_mng_write_commit(hw);
        }
    }
    return ret_val;
}


/*****************************************************************************
 * This function calculates the checksum.
 *
 * returns  - checksum of buffer contents.
 ****************************************************************************/
static uint8_t
e1000_calculate_mng_checksum(char *buffer, uint32_t length)
{
    uint8_t sum = 0;
    uint32_t i;

    if (!buffer)
        return 0;

    for (i=0; i < length; i++)
        sum += buffer[i];

    return (uint8_t) (0 - sum);
}

/*****************************************************************************
 * This function checks whether tx pkt filtering needs to be enabled or not.
 *
 * returns  - TRUE for packet filtering or FALSE.
 ****************************************************************************/
boolean_t
e1000_enable_tx_pkt_filtering(struct e1000_hw *hw)
{
    /* called in init as well as watchdog timer functions */

    int32_t ret_val, checksum;
    boolean_t tx_filter = FALSE;
    struct e1000_host_mng_dhcp_cookie *hdr = &(hw->mng_cookie);
    uint8_t *buffer = (uint8_t *) &(hw->mng_cookie);

    if (e1000_check_mng_mode(hw)) {
        ret_val = e1000_mng_enable_host_if(hw);
        if (ret_val == E1000_SUCCESS) {
            ret_val = e1000_host_if_read_cookie(hw, buffer);
            if (ret_val == E1000_SUCCESS) {
                checksum = hdr->checksum;
                hdr->checksum = 0;
                if ((hdr->signature == E1000_IAMT_SIGNATURE) &&
                    checksum == e1000_calculate_mng_checksum((char *)buffer,
                                               E1000_MNG_DHCP_COOKIE_LENGTH)) {
                    if (hdr->status &
                        E1000_MNG_DHCP_COOKIE_STATUS_PARSING_SUPPORT)
                        tx_filter = TRUE;
                } else
                    tx_filter = TRUE;
            } else
                tx_filter = TRUE;
        }
    }

    hw->tx_pkt_filtering = tx_filter;
    return tx_filter;
}

/******************************************************************************
 * Verifies the hardware needs to allow ARPs to be processed by the host
 *
 * hw - Struct containing variables accessed by shared code
 *
 * returns: - TRUE/FALSE
 *
 *****************************************************************************/
uint32_t
e1000_enable_mng_pass_thru(struct e1000_hw *hw)
{
    uint32_t manc;
    uint32_t fwsm, factps;

    if (hw->asf_firmware_present) {
        manc = E1000_READ_REG(hw, MANC);

        if (!(manc & E1000_MANC_RCV_TCO_EN) ||
            !(manc & E1000_MANC_EN_MAC_ADDR_FILTER))
            return FALSE;
        if (e1000_arc_subsystem_valid(hw) == TRUE) {
            fwsm = E1000_READ_REG(hw, FWSM);
            factps = E1000_READ_REG(hw, FACTPS);

            if (((fwsm & E1000_FWSM_MODE_MASK) ==
                (e1000_mng_mode_pt << E1000_FWSM_MODE_SHIFT)) &&
                (factps & E1000_FACTPS_MNGCG))
                return TRUE;
        } else
            if ((manc & E1000_MANC_SMBUS_EN) && !(manc & E1000_MANC_ASF_EN))
                return TRUE;
    }
    return FALSE;
}

static int32_t
e1000_polarity_reversal_workaround(struct e1000_hw *hw)
{
    int32_t ret_val;
    uint16_t mii_status_reg;
    uint16_t i;

    /* Polarity reversal workaround for forced 10F/10H links. */

    /* Disable the transmitter on the PHY */

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
    if (ret_val)
        return ret_val;
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFFF);
    if (ret_val)
        return ret_val;

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
    if (ret_val)
        return ret_val;

    /* This loop will early-out if the NO link condition has been met. */
    for (i = PHY_FORCE_TIME; i > 0; i--) {
        /* Read the MII Status Register and wait for Link Status bit
         * to be clear.
         */

        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;

        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;

        if ((mii_status_reg & ~MII_SR_LINK_STATUS) == 0) break;
        mdelay(100);
    }

    /* Recommended delay time after link has been lost */
    mdelay(1000);

    /* Now we will re-enable th transmitter on the PHY */

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
    if (ret_val)
        return ret_val;
    mdelay(50);
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFF0);
    if (ret_val)
        return ret_val;
    mdelay(50);
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFF00);
    if (ret_val)
        return ret_val;
    mdelay(50);
    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0x0000);
    if (ret_val)
        return ret_val;

    ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
    if (ret_val)
        return ret_val;

    /* This loop will early-out if the link condition has been met. */
    for (i = PHY_FORCE_TIME; i > 0; i--) {
        /* Read the MII Status Register and wait for Link Status bit
         * to be set.
         */

        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;

        ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
        if (ret_val)
            return ret_val;

        if (mii_status_reg & MII_SR_LINK_STATUS) break;
        mdelay(100);
    }
    return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Disables PCI-Express master access.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - none.
 *
 ***************************************************************************/
static void
e1000_set_pci_express_master_disable(struct e1000_hw *hw)
{
    uint32_t ctrl;

    DEBUGFUNC("e1000_set_pci_express_master_disable");

    if (hw->bus_type != e1000_bus_type_pci_express)
        return;

    ctrl = E1000_READ_REG(hw, CTRL);
    ctrl |= E1000_CTRL_GIO_MASTER_DISABLE;
    E1000_WRITE_REG(hw, CTRL, ctrl);
}

/*******************************************************************************
 *
 * Disables PCI-Express master access and verifies there are no pending requests
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_MASTER_REQUESTS_PENDING if master disable bit hasn't
 *            caused the master requests to be disabled.
 *            E1000_SUCCESS master requests disabled.
 *
 ******************************************************************************/
int32_t
e1000_disable_pciex_master(struct e1000_hw *hw)
{
    int32_t timeout = MASTER_DISABLE_TIMEOUT;   /* 80ms */

    DEBUGFUNC("e1000_disable_pciex_master");

    if (hw->bus_type != e1000_bus_type_pci_express)
        return E1000_SUCCESS;

    e1000_set_pci_express_master_disable(hw);

    while (timeout) {
        if (!(E1000_READ_REG(hw, STATUS) & E1000_STATUS_GIO_MASTER_ENABLE))
            break;
        else
            udelay(100);
        timeout--;
    }

    if (!timeout) {
        DEBUGOUT("Master requests are pending.\n");
        return -E1000_ERR_MASTER_REQUESTS_PENDING;
    }

    return E1000_SUCCESS;
}

/*******************************************************************************
 *
 * Check for EEPROM Auto Read bit done.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 *
 ******************************************************************************/
static int32_t
e1000_get_auto_rd_done(struct e1000_hw *hw)
{
    int32_t timeout = AUTO_READ_DONE_TIMEOUT;

    DEBUGFUNC("e1000_get_auto_rd_done");

    switch (hw->mac_type) {
    default:
        msleep(5);
        break;
    case e1000_82571:
    case e1000_82572:
    case e1000_82573:
    case e1000_80003es2lan:
    case e1000_ich8lan:
        while (timeout) {
            if (E1000_READ_REG(hw, EECD) & E1000_EECD_AUTO_RD)
                break;
            else msleep(1);
            timeout--;
        }

        if (!timeout) {
            DEBUGOUT("Auto read by HW from EEPROM has not completed.\n");
            return -E1000_ERR_RESET;
        }
        break;
    }

    /* PHY configuration from NVM just starts after EECD_AUTO_RD sets to high.
     * Need to wait for PHY configuration completion before accessing NVM
     * and PHY. */
    if (hw->mac_type == e1000_82573)
        msleep(25);

    return E1000_SUCCESS;
}

/***************************************************************************
 * Checks if the PHY configuration is done
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
static int32_t
e1000_get_phy_cfg_done(struct e1000_hw *hw)
{
    int32_t timeout = PHY_CFG_TIMEOUT;
    uint32_t cfg_mask = E1000_EEPROM_CFG_DONE;

    DEBUGFUNC("e1000_get_phy_cfg_done");

    switch (hw->mac_type) {
    default:
        mdelay(10);
        break;
    case e1000_80003es2lan:
        /* Separate *_CFG_DONE_* bit for each port */
        if (E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)
            cfg_mask = E1000_EEPROM_CFG_DONE_PORT_1;
        /* Fall Through */
    case e1000_82571:
    case e1000_82572:
        while (timeout) {
            if (E1000_READ_REG(hw, EEMNGCTL) & cfg_mask)
                break;
            else
                msleep(1);
            timeout--;
        }
        if (!timeout) {
            DEBUGOUT("MNG configuration cycle has not completed.\n");
            return -E1000_ERR_RESET;
        }
        break;
    }

    return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Using the combination of SMBI and SWESMBI semaphore bits when resetting
 * adapter or Eeprom access.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_EEPROM if fail to access EEPROM.
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
static int32_t
e1000_get_hw_eeprom_semaphore(struct e1000_hw *hw)
{
    int32_t timeout;
    uint32_t swsm;

    DEBUGFUNC("e1000_get_hw_eeprom_semaphore");

    if (!hw->eeprom_semaphore_present)
        return E1000_SUCCESS;

    if (hw->mac_type == e1000_80003es2lan) {
        /* Get the SW semaphore. */
        if (e1000_get_software_semaphore(hw) != E1000_SUCCESS)
            return -E1000_ERR_EEPROM;
    }

    /* Get the FW semaphore. */
    timeout = hw->eeprom.word_size + 1;
    while (timeout) {
        swsm = E1000_READ_REG(hw, SWSM);
        swsm |= E1000_SWSM_SWESMBI;
        E1000_WRITE_REG(hw, SWSM, swsm);
        /* if we managed to set the bit we got the semaphore. */
        swsm = E1000_READ_REG(hw, SWSM);
        if (swsm & E1000_SWSM_SWESMBI)
            break;

        udelay(50);
        timeout--;
    }

    if (!timeout) {
        /* Release semaphores */
        e1000_put_hw_eeprom_semaphore(hw);
        DEBUGOUT("Driver can't access the Eeprom - SWESMBI bit is set.\n");
        return -E1000_ERR_EEPROM;
    }

    return E1000_SUCCESS;
}

/***************************************************************************
 * This function clears HW semaphore bits.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - None.
 *
 ***************************************************************************/
static void
e1000_put_hw_eeprom_semaphore(struct e1000_hw *hw)
{
    uint32_t swsm;

    DEBUGFUNC("e1000_put_hw_eeprom_semaphore");

    if (!hw->eeprom_semaphore_present)
        return;

    swsm = E1000_READ_REG(hw, SWSM);
    if (hw->mac_type == e1000_80003es2lan) {
        /* Release both semaphores. */
        swsm &= ~(E1000_SWSM_SMBI | E1000_SWSM_SWESMBI);
    } else
        swsm &= ~(E1000_SWSM_SWESMBI);
    E1000_WRITE_REG(hw, SWSM, swsm);
}

/***************************************************************************
 *
 * Obtaining software semaphore bit (SMBI) before resetting PHY.
 *
 * hw: Struct containing variables accessed by shared code
 *
 * returns: - E1000_ERR_RESET if fail to obtain semaphore.
 *            E1000_SUCCESS at any other case.
 *
 ***************************************************************************/
static int32_t
e1000_get_software_semaphore(struct e1000_hw *hw)
{
    int32_t timeout = hw->eeprom.word_size + 1;
    uint32_t swsm;

    DEBUGFUNC("e1000_get_software_semaphore");

    if (hw->mac_type != e1000_80003es2lan) {
        return E1000_SUCCESS;
    }

    while (timeout) {
        swsm = E1000_READ_REG(hw, SWSM);
        /* If SMBI bit cleared, it is now set and we hold the semaphore */
        if (!(swsm & E1000_SWSM_SMBI))
            break;
        mdelay(1);
        timeout--;
    }

    if (!timeout) {
        DEBUGOUT("Driver can't access device - SMBI bit is set.\n");
        return -E1000_ERR_RESET;
    }

    return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Release semaphore bit (SMBI).
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
static void
e1000_release_software_semaphore(struct e1000_hw *hw)
{
    uint32_t swsm;

    DEBUGFUNC("e1000_release_software_semaphore");

    if (hw->mac_type != e1000_80003es2lan) {
        return;
    }

    swsm = E1000_READ_REG(hw, SWSM);
    /* Release the SW semaphores.*/
    swsm &= ~E1000_SWSM_SMBI;
    E1000_WRITE_REG(hw, SWSM, swsm);
}

/******************************************************************************
 * Checks if PHY reset is blocked due to SOL/IDER session, for example.
 * Returning E1000_BLK_PHY_RESET isn't necessarily an error.  But it's up to
 * the caller to figure out how to deal with it.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * returns: - E1000_BLK_PHY_RESET
 *            E1000_SUCCESS
 *
 *****************************************************************************/
int32_t
e1000_check_phy_reset_block(struct e1000_hw *hw)
{
    uint32_t manc = 0;
    uint32_t fwsm = 0;

    if (hw->mac_type == e1000_ich8lan) {
        fwsm = E1000_READ_REG(hw, FWSM);
        return (fwsm & E1000_FWSM_RSPCIPHY) ? E1000_SUCCESS
                                            : E1000_BLK_PHY_RESET;
    }

    if (hw->mac_type > e1000_82547_rev_2)
        manc = E1000_READ_REG(hw, MANC);
    return (manc & E1000_MANC_BLK_PHY_RST_ON_IDE) ?
        E1000_BLK_PHY_RESET : E1000_SUCCESS;
}

static uint8_t
e1000_arc_subsystem_valid(struct e1000_hw *hw)
{
    uint32_t fwsm;

    /* On 8257x silicon, registers in the range of 0x8800 - 0x8FFC
     * may not be provided a DMA clock when no manageability features are
     * enabled.  We do not want to perform any reads/writes to these registers
     * if this is the case.  We read FWSM to determine the manageability mode.
     */
    switch (hw->mac_type) {
    case e1000_82571:
    case e1000_82572:
    case e1000_82573:
    case e1000_80003es2lan:
        fwsm = E1000_READ_REG(hw, FWSM);
        if ((fwsm & E1000_FWSM_MODE_MASK) != 0)
            return TRUE;
        break;
    case e1000_ich8lan:
        return TRUE;
    default:
        break;
    }
    return FALSE;
}


/******************************************************************************
 * Configure PCI-Ex no-snoop
 *
 * hw - Struct containing variables accessed by shared code.
 * no_snoop - Bitmap of no-snoop events.
 *
 * returns: E1000_SUCCESS
 *
 *****************************************************************************/
static int32_t
e1000_set_pci_ex_no_snoop(struct e1000_hw *hw, uint32_t no_snoop)
{
    uint32_t gcr_reg = 0;

    DEBUGFUNC("e1000_set_pci_ex_no_snoop");

    if (hw->bus_type == e1000_bus_type_unknown)
        e1000_get_bus_info(hw);

    if (hw->bus_type != e1000_bus_type_pci_express)
        return E1000_SUCCESS;

    if (no_snoop) {
        gcr_reg = E1000_READ_REG(hw, GCR);
        gcr_reg &= ~(PCI_EX_NO_SNOOP_ALL);
        gcr_reg |= no_snoop;
        E1000_WRITE_REG(hw, GCR, gcr_reg);
    }
    if (hw->mac_type == e1000_ich8lan) {
        uint32_t ctrl_ext;

        E1000_WRITE_REG(hw, GCR, PCI_EX_82566_SNOOP_ALL);

        ctrl_ext = E1000_READ_REG(hw, CTRL_EXT);
        ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
        E1000_WRITE_REG(hw, CTRL_EXT, ctrl_ext);
    }

    return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Get software semaphore FLAG bit (SWFLAG).
 * SWFLAG is used to synchronize the access to all shared resource between
 * SW, FW and HW.
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
static int32_t
e1000_get_software_flag(struct e1000_hw *hw)
{
    int32_t timeout = PHY_CFG_TIMEOUT;
    uint32_t extcnf_ctrl;

    DEBUGFUNC("e1000_get_software_flag");

    if (hw->mac_type == e1000_ich8lan) {
        while (timeout) {
            extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
            extcnf_ctrl |= E1000_EXTCNF_CTRL_SWFLAG;
            E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);

            extcnf_ctrl = E1000_READ_REG(hw, EXTCNF_CTRL);
            if (extcnf_ctrl & E1000_EXTCNF_CTRL_SWFLAG)
                break;
            mdelay(1);
            timeout--;
        }

        if (!timeout) {
            DEBUGOUT("FW or HW locks the resource too long.\n");
            return -E1000_ERR_CONFIG;
        }
    }

    return E1000_SUCCESS;
}

/***************************************************************************
 *
 * Release software semaphore FLAG bit (SWFLAG).
 * SWFLAG is used to synchronize the access to all shared resource between
 * SW, FW and HW.
 *
 * hw: Struct containing variables accessed by shared code
 *
 ***************************************************************************/
static void
e1000_release_software_flag(struct e1000_hw *hw)
{
    uint32_t extcnf_ctrl;

    DEBUGFUNC("e1000_release_software_flag");

    if (hw->mac_type == e1000_ich8lan) {
        extcnf_ctrl= E1000_READ_REG(hw, EXTCNF_CTRL);
        extcnf_ctrl &= ~E1000_EXTCNF_CTRL_SWFLAG;
        E1000_WRITE_REG(hw, EXTCNF_CTRL, extcnf_ctrl);
    }

    return;
}

/******************************************************************************
 * Reads a 16 bit word or words from the EEPROM using the ICH8's flash access
 * register.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the EEPROM to read
 * data - word read from the EEPROM
 * words - number of words to read
 *****************************************************************************/
static int32_t
e1000_read_eeprom_ich8(struct e1000_hw *hw, uint16_t offset, uint16_t words,
                       uint16_t *data)
{
    int32_t  error = E1000_SUCCESS;
    uint32_t flash_bank = 0;
    uint32_t act_offset = 0;
    uint32_t bank_offset = 0;
    uint16_t word = 0;
    uint16_t i = 0;

    /* We need to know which is the valid flash bank.  In the event
     * that we didn't allocate eeprom_shadow_ram, we may not be
     * managing flash_bank.  So it cannot be trusted and needs
     * to be updated with each read.
     */
    /* Value of bit 22 corresponds to the flash bank we're on. */
    flash_bank = (E1000_READ_REG(hw, EECD) & E1000_EECD_SEC1VAL) ? 1 : 0;

    /* Adjust offset appropriately if we're on bank 1 - adjust for word size */
    bank_offset = flash_bank * (hw->flash_bank_size * 2);

    error = e1000_get_software_flag(hw);
    if (error != E1000_SUCCESS)
        return error;

    for (i = 0; i < words; i++) {
        if (hw->eeprom_shadow_ram != NULL &&
            hw->eeprom_shadow_ram[offset+i].modified == TRUE) {
            data[i] = hw->eeprom_shadow_ram[offset+i].eeprom_word;
        } else {
            /* The NVM part needs a byte offset, hence * 2 */
            act_offset = bank_offset + ((offset + i) * 2);
            error = e1000_read_ich8_word(hw, act_offset, &word);
            if (error != E1000_SUCCESS)
                break;
            data[i] = word;
        }
    }

    e1000_release_software_flag(hw);

    return error;
}

/******************************************************************************
 * Writes a 16 bit word or words to the EEPROM using the ICH8's flash access
 * register.  Actually, writes are written to the shadow ram cache in the hw
 * structure hw->e1000_shadow_ram.  e1000_commit_shadow_ram flushes this to
 * the NVM, which occurs when the NVM checksum is updated.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of word in the EEPROM to write
 * words - number of words to write
 * data - words to write to the EEPROM
 *****************************************************************************/
static int32_t
e1000_write_eeprom_ich8(struct e1000_hw *hw, uint16_t offset, uint16_t words,
                        uint16_t *data)
{
    uint32_t i = 0;
    int32_t error = E1000_SUCCESS;

    error = e1000_get_software_flag(hw);
    if (error != E1000_SUCCESS)
        return error;

    /* A driver can write to the NVM only if it has eeprom_shadow_ram
     * allocated.  Subsequent reads to the modified words are read from
     * this cached structure as well.  Writes will only go into this
     * cached structure unless it's followed by a call to
     * e1000_update_eeprom_checksum() where it will commit the changes
     * and clear the "modified" field.
     */
    if (hw->eeprom_shadow_ram != NULL) {
        for (i = 0; i < words; i++) {
            if ((offset + i) < E1000_SHADOW_RAM_WORDS) {
                hw->eeprom_shadow_ram[offset+i].modified = TRUE;
                hw->eeprom_shadow_ram[offset+i].eeprom_word = data[i];
            } else {
                error = -E1000_ERR_EEPROM;
                break;
            }
        }
    } else {
        /* Drivers have the option to not allocate eeprom_shadow_ram as long
         * as they don't perform any NVM writes.  An attempt in doing so
         * will result in this error.
         */
        error = -E1000_ERR_EEPROM;
    }

    e1000_release_software_flag(hw);

    return error;
}

/******************************************************************************
 * This function does initial flash setup so that a new read/write/erase cycle
 * can be started.
 *
 * hw - The pointer to the hw structure
 ****************************************************************************/
static int32_t
e1000_ich8_cycle_init(struct e1000_hw *hw)
{
    union ich8_hws_flash_status hsfsts;
    int32_t error = E1000_ERR_EEPROM;
    int32_t i     = 0;

    DEBUGFUNC("e1000_ich8_cycle_init");

    hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);

    /* May be check the Flash Des Valid bit in Hw status */
    if (hsfsts.hsf_status.fldesvalid == 0) {
        DEBUGOUT("Flash descriptor invalid.  SW Sequencing must be used.");
        return error;
    }

    /* Clear FCERR in Hw status by writing 1 */
    /* Clear DAEL in Hw status by writing a 1 */
    hsfsts.hsf_status.flcerr = 1;
    hsfsts.hsf_status.dael = 1;

    E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFSTS, hsfsts.regval);

    /* Either we should have a hardware SPI cycle in progress bit to check
     * against, in order to start a new cycle or FDONE bit should be changed
     * in the hardware so that it is 1 after harware reset, which can then be
     * used as an indication whether a cycle is in progress or has been
     * completed .. we should also have some software semaphore mechanism to
     * guard FDONE or the cycle in progress bit so that two threads access to
     * those bits can be sequentiallized or a way so that 2 threads dont
     * start the cycle at the same time */

    if (hsfsts.hsf_status.flcinprog == 0) {
        /* There is no cycle running at present, so we can start a cycle */
        /* Begin by setting Flash Cycle Done. */
        hsfsts.hsf_status.flcdone = 1;
        E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFSTS, hsfsts.regval);
        error = E1000_SUCCESS;
    } else {
        /* otherwise poll for sometime so the current cycle has a chance
         * to end before giving up. */
        for (i = 0; i < ICH8_FLASH_COMMAND_TIMEOUT; i++) {
            hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);
            if (hsfsts.hsf_status.flcinprog == 0) {
                error = E1000_SUCCESS;
                break;
            }
            udelay(1);
        }
        if (error == E1000_SUCCESS) {
            /* Successful in waiting for previous cycle to timeout,
             * now set the Flash Cycle Done. */
            hsfsts.hsf_status.flcdone = 1;
            E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFSTS, hsfsts.regval);
        } else {
            DEBUGOUT("Flash controller busy, cannot get access");
        }
    }
    return error;
}

/******************************************************************************
 * This function starts a flash cycle and waits for its completion
 *
 * hw - The pointer to the hw structure
 ****************************************************************************/
static int32_t
e1000_ich8_flash_cycle(struct e1000_hw *hw, uint32_t timeout)
{
    union ich8_hws_flash_ctrl hsflctl;
    union ich8_hws_flash_status hsfsts;
    int32_t error = E1000_ERR_EEPROM;
    uint32_t i = 0;

    /* Start a cycle by writing 1 in Flash Cycle Go in Hw Flash Control */
    hsflctl.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFCTL);
    hsflctl.hsf_ctrl.flcgo = 1;
    E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFCTL, hsflctl.regval);

    /* wait till FDONE bit is set to 1 */
    do {
        hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);
        if (hsfsts.hsf_status.flcdone == 1)
            break;
        udelay(1);
        i++;
    } while (i < timeout);
    if (hsfsts.hsf_status.flcdone == 1 && hsfsts.hsf_status.flcerr == 0) {
        error = E1000_SUCCESS;
    }
    return error;
}

/******************************************************************************
 * Reads a byte or word from the NVM using the ICH8 flash access registers.
 *
 * hw - The pointer to the hw structure
 * index - The index of the byte or word to read.
 * size - Size of data to read, 1=byte 2=word
 * data - Pointer to the word to store the value read.
 *****************************************************************************/
static int32_t
e1000_read_ich8_data(struct e1000_hw *hw, uint32_t index,
                     uint32_t size, uint16_t* data)
{
    union ich8_hws_flash_status hsfsts;
    union ich8_hws_flash_ctrl hsflctl;
    uint32_t flash_linear_address;
    uint32_t flash_data = 0;
    int32_t error = -E1000_ERR_EEPROM;
    int32_t count = 0;

    DEBUGFUNC("e1000_read_ich8_data");

    if (size < 1  || size > 2 || data == 0x0 ||
        index > ICH8_FLASH_LINEAR_ADDR_MASK)
        return error;

    flash_linear_address = (ICH8_FLASH_LINEAR_ADDR_MASK & index) +
                           hw->flash_base_addr;

    do {
        udelay(1);
        /* Steps */
        error = e1000_ich8_cycle_init(hw);
        if (error != E1000_SUCCESS)
            break;

        hsflctl.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFCTL);
        /* 0b/1b corresponds to 1 or 2 byte size, respectively. */
        hsflctl.hsf_ctrl.fldbcount = size - 1;
        hsflctl.hsf_ctrl.flcycle = ICH8_CYCLE_READ;
        E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFCTL, hsflctl.regval);

        /* Write the last 24 bits of index into Flash Linear address field in
         * Flash Address */
        /* TODO: TBD maybe check the index against the size of flash */

        E1000_WRITE_ICH8_REG(hw, ICH8_FLASH_FADDR, flash_linear_address);

        error = e1000_ich8_flash_cycle(hw, ICH8_FLASH_COMMAND_TIMEOUT);

        /* Check if FCERR is set to 1, if set to 1, clear it and try the whole
         * sequence a few more times, else read in (shift in) the Flash Data0,
         * the order is least significant byte first msb to lsb */
        if (error == E1000_SUCCESS) {
            flash_data = E1000_READ_ICH8_REG(hw, ICH8_FLASH_FDATA0);
            if (size == 1) {
                *data = (uint8_t)(flash_data & 0x000000FF);
            } else if (size == 2) {
                *data = (uint16_t)(flash_data & 0x0000FFFF);
            }
            break;
        } else {
            /* If we've gotten here, then things are probably completely hosed,
             * but if the error condition is detected, it won't hurt to give
             * it another try...ICH8_FLASH_CYCLE_REPEAT_COUNT times.
             */
            hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);
            if (hsfsts.hsf_status.flcerr == 1) {
                /* Repeat for some time before giving up. */
                continue;
            } else if (hsfsts.hsf_status.flcdone == 0) {
                DEBUGOUT("Timeout error - flash cycle did not complete.");
                break;
            }
        }
    } while (count++ < ICH8_FLASH_CYCLE_REPEAT_COUNT);

    return error;
}

/******************************************************************************
 * Writes One /two bytes to the NVM using the ICH8 flash access registers.
 *
 * hw - The pointer to the hw structure
 * index - The index of the byte/word to read.
 * size - Size of data to read, 1=byte 2=word
 * data - The byte(s) to write to the NVM.
 *****************************************************************************/
static int32_t
e1000_write_ich8_data(struct e1000_hw *hw, uint32_t index, uint32_t size,
                      uint16_t data)
{
    union ich8_hws_flash_status hsfsts;
    union ich8_hws_flash_ctrl hsflctl;
    uint32_t flash_linear_address;
    uint32_t flash_data = 0;
    int32_t error = -E1000_ERR_EEPROM;
    int32_t count = 0;

    DEBUGFUNC("e1000_write_ich8_data");

    if (size < 1  || size > 2 || data > size * 0xff ||
        index > ICH8_FLASH_LINEAR_ADDR_MASK)
        return error;

    flash_linear_address = (ICH8_FLASH_LINEAR_ADDR_MASK & index) +
                           hw->flash_base_addr;

    do {
        udelay(1);
        /* Steps */
        error = e1000_ich8_cycle_init(hw);
        if (error != E1000_SUCCESS)
            break;

        hsflctl.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFCTL);
        /* 0b/1b corresponds to 1 or 2 byte size, respectively. */
        hsflctl.hsf_ctrl.fldbcount = size -1;
        hsflctl.hsf_ctrl.flcycle = ICH8_CYCLE_WRITE;
        E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFCTL, hsflctl.regval);

        /* Write the last 24 bits of index into Flash Linear address field in
         * Flash Address */
        E1000_WRITE_ICH8_REG(hw, ICH8_FLASH_FADDR, flash_linear_address);

        if (size == 1)
            flash_data = (uint32_t)data & 0x00FF;
        else
            flash_data = (uint32_t)data;

        E1000_WRITE_ICH8_REG(hw, ICH8_FLASH_FDATA0, flash_data);

        /* check if FCERR is set to 1 , if set to 1, clear it and try the whole
         * sequence a few more times else done */
        error = e1000_ich8_flash_cycle(hw, ICH8_FLASH_COMMAND_TIMEOUT);
        if (error == E1000_SUCCESS) {
            break;
        } else {
            /* If we're here, then things are most likely completely hosed,
             * but if the error condition is detected, it won't hurt to give
             * it another try...ICH8_FLASH_CYCLE_REPEAT_COUNT times.
             */
            hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);
            if (hsfsts.hsf_status.flcerr == 1) {
                /* Repeat for some time before giving up. */
                continue;
            } else if (hsfsts.hsf_status.flcdone == 0) {
                DEBUGOUT("Timeout error - flash cycle did not complete.");
                break;
            }
        }
    } while (count++ < ICH8_FLASH_CYCLE_REPEAT_COUNT);

    return error;
}

/******************************************************************************
 * Reads a single byte from the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to e1000_hw structure
 * index - The index of the byte to read.
 * data - Pointer to a byte to store the value read.
 *****************************************************************************/
static int32_t
e1000_read_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t* data)
{
    int32_t status = E1000_SUCCESS;
    uint16_t word = 0;

    status = e1000_read_ich8_data(hw, index, 1, &word);
    if (status == E1000_SUCCESS) {
        *data = (uint8_t)word;
    }

    return status;
}

/******************************************************************************
 * Writes a single byte to the NVM using the ICH8 flash access registers.
 * Performs verification by reading back the value and then going through
 * a retry algorithm before giving up.
 *
 * hw - pointer to e1000_hw structure
 * index - The index of the byte to write.
 * byte - The byte to write to the NVM.
 *****************************************************************************/
static int32_t
e1000_verify_write_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t byte)
{
    int32_t error = E1000_SUCCESS;
    int32_t program_retries = 0;

    DEBUGOUT2("Byte := %2.2X Offset := %d\n", byte, index);

    error = e1000_write_ich8_byte(hw, index, byte);

    if (error != E1000_SUCCESS) {
        for (program_retries = 0; program_retries < 100; program_retries++) {
            DEBUGOUT2("Retrying \t Byte := %2.2X Offset := %d\n", byte, index);
            error = e1000_write_ich8_byte(hw, index, byte);
            udelay(100);
            if (error == E1000_SUCCESS)
                break;
        }
    }

    if (program_retries == 100)
        error = E1000_ERR_EEPROM;

    return error;
}

/******************************************************************************
 * Writes a single byte to the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to e1000_hw structure
 * index - The index of the byte to read.
 * data - The byte to write to the NVM.
 *****************************************************************************/
static int32_t
e1000_write_ich8_byte(struct e1000_hw *hw, uint32_t index, uint8_t data)
{
    int32_t status = E1000_SUCCESS;
    uint16_t word = (uint16_t)data;

    status = e1000_write_ich8_data(hw, index, 1, word);

    return status;
}

/******************************************************************************
 * Reads a word from the NVM using the ICH8 flash access registers.
 *
 * hw - pointer to e1000_hw structure
 * index - The starting byte index of the word to read.
 * data - Pointer to a word to store the value read.
 *****************************************************************************/
static int32_t
e1000_read_ich8_word(struct e1000_hw *hw, uint32_t index, uint16_t *data)
{
    int32_t status = E1000_SUCCESS;
    status = e1000_read_ich8_data(hw, index, 2, data);
    return status;
}

/******************************************************************************
 * Erases the bank specified. Each bank may be a 4, 8 or 64k block. Banks are 0
 * based.
 *
 * hw - pointer to e1000_hw structure
 * bank - 0 for first bank, 1 for second bank
 *
 * Note that this function may actually erase as much as 8 or 64 KBytes.  The
 * amount of NVM used in each bank is a *minimum* of 4 KBytes, but in fact the
 * bank size may be 4, 8 or 64 KBytes
 *****************************************************************************/
int32_t
e1000_erase_ich8_4k_segment(struct e1000_hw *hw, uint32_t bank)
{
    union ich8_hws_flash_status hsfsts;
    union ich8_hws_flash_ctrl hsflctl;
    uint32_t flash_linear_address;
    int32_t  count = 0;
    int32_t  error = E1000_ERR_EEPROM;
    int32_t  iteration;
    int32_t  sub_sector_size = 0;
    int32_t  bank_size;
    int32_t  j = 0;
    int32_t  error_flag = 0;

    hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);

    /* Determine HW Sector size: Read BERASE bits of Hw flash Status register */
    /* 00: The Hw sector is 256 bytes, hence we need to erase 16
     *     consecutive sectors.  The start index for the nth Hw sector can be
     *     calculated as bank * 4096 + n * 256
     * 01: The Hw sector is 4K bytes, hence we need to erase 1 sector.
     *     The start index for the nth Hw sector can be calculated
     *     as bank * 4096
     * 10: The HW sector is 8K bytes
     * 11: The Hw sector size is 64K bytes */
    if (hsfsts.hsf_status.berasesz == 0x0) {
        /* Hw sector size 256 */
        sub_sector_size = ICH8_FLASH_SEG_SIZE_256;
        bank_size = ICH8_FLASH_SECTOR_SIZE;
        iteration = ICH8_FLASH_SECTOR_SIZE / ICH8_FLASH_SEG_SIZE_256;
    } else if (hsfsts.hsf_status.berasesz == 0x1) {
        bank_size = ICH8_FLASH_SEG_SIZE_4K;
        iteration = 1;
    } else if (hw->mac_type != e1000_ich8lan &&
               hsfsts.hsf_status.berasesz == 0x2) {
        /* 8K erase size invalid for ICH8 - added in for ICH9 */
        bank_size = ICH9_FLASH_SEG_SIZE_8K;
        iteration = 1;
    } else if (hsfsts.hsf_status.berasesz == 0x3) {
        bank_size = ICH8_FLASH_SEG_SIZE_64K;
        iteration = 1;
    } else {
        return error;
    }

    for (j = 0; j < iteration ; j++) {
        do {
            count++;
            /* Steps */
            error = e1000_ich8_cycle_init(hw);
            if (error != E1000_SUCCESS) {
                error_flag = 1;
                break;
            }

            /* Write a value 11 (block Erase) in Flash Cycle field in Hw flash
             * Control */
            hsflctl.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFCTL);
            hsflctl.hsf_ctrl.flcycle = ICH8_CYCLE_ERASE;
            E1000_WRITE_ICH8_REG16(hw, ICH8_FLASH_HSFCTL, hsflctl.regval);

            /* Write the last 24 bits of an index within the block into Flash
             * Linear address field in Flash Address.  This probably needs to
             * be calculated here based off the on-chip erase sector size and
             * the software bank size (4, 8 or 64 KBytes) */
            flash_linear_address = bank * bank_size + j * sub_sector_size;
            flash_linear_address += hw->flash_base_addr;
            flash_linear_address &= ICH8_FLASH_LINEAR_ADDR_MASK;

            E1000_WRITE_ICH8_REG(hw, ICH8_FLASH_FADDR, flash_linear_address);

            error = e1000_ich8_flash_cycle(hw, ICH8_FLASH_ERASE_TIMEOUT);
            /* Check if FCERR is set to 1.  If 1, clear it and try the whole
             * sequence a few more times else Done */
            if (error == E1000_SUCCESS) {
                break;
            } else {
                hsfsts.regval = E1000_READ_ICH8_REG16(hw, ICH8_FLASH_HSFSTS);
                if (hsfsts.hsf_status.flcerr == 1) {
                    /* repeat for some time before giving up */
                    continue;
                } else if (hsfsts.hsf_status.flcdone == 0) {
                    error_flag = 1;
                    break;
                }
            }
        } while ((count < ICH8_FLASH_CYCLE_REPEAT_COUNT) && !error_flag);
        if (error_flag == 1)
            break;
    }
    if (error_flag != 1)
        error = E1000_SUCCESS;
    return error;
}

static int32_t
e1000_init_lcd_from_nvm_config_region(struct e1000_hw *hw,
                                      uint32_t cnf_base_addr, uint32_t cnf_size)
{
    uint32_t ret_val = E1000_SUCCESS;
    uint16_t word_addr, reg_data, reg_addr;
    uint16_t i;

    /* cnf_base_addr is in DWORD */
    word_addr = (uint16_t)(cnf_base_addr << 1);

    /* cnf_size is returned in size of dwords */
    for (i = 0; i < cnf_size; i++) {
        ret_val = e1000_read_eeprom(hw, (word_addr + i*2), 1, &reg_data);
        if (ret_val)
            return ret_val;

        ret_val = e1000_read_eeprom(hw, (word_addr + i*2 + 1), 1, &reg_addr);
        if (ret_val)
            return ret_val;

        ret_val = e1000_get_software_flag(hw);
        if (ret_val != E1000_SUCCESS)
            return ret_val;

        ret_val = e1000_write_phy_reg_ex(hw, (uint32_t)reg_addr, reg_data);

        e1000_release_software_flag(hw);
    }

    return ret_val;
}


/******************************************************************************
 * This function initializes the PHY from the NVM on ICH8 platforms. This
 * is needed due to an issue where the NVM configuration is not properly
 * autoloaded after power transitions. Therefore, after each PHY reset, we
 * will load the configuration data out of the NVM manually.
 *
 * hw: Struct containing variables accessed by shared code
 *****************************************************************************/
static int32_t
e1000_init_lcd_from_nvm(struct e1000_hw *hw)
{
    uint32_t reg_data, cnf_base_addr, cnf_size, ret_val, loop;

    if (hw->phy_type != e1000_phy_igp_3)
          return E1000_SUCCESS;

    /* Check if SW needs configure the PHY */
    reg_data = E1000_READ_REG(hw, FEXTNVM);
    if (!(reg_data & FEXTNVM_SW_CONFIG))
        return E1000_SUCCESS;

    /* Wait for basic configuration completes before proceeding*/
    loop = 0;
    do {
        reg_data = E1000_READ_REG(hw, STATUS) & E1000_STATUS_LAN_INIT_DONE;
        udelay(100);
        loop++;
    } while ((!reg_data) && (loop < 50));

    /* Clear the Init Done bit for the next init event */
    reg_data = E1000_READ_REG(hw, STATUS);
    reg_data &= ~E1000_STATUS_LAN_INIT_DONE;
    E1000_WRITE_REG(hw, STATUS, reg_data);

    /* Make sure HW does not configure LCD from PHY extended configuration
       before SW configuration */
    reg_data = E1000_READ_REG(hw, EXTCNF_CTRL);
    if ((reg_data & E1000_EXTCNF_CTRL_LCD_WRITE_ENABLE) == 0x0000) {
        reg_data = E1000_READ_REG(hw, EXTCNF_SIZE);
        cnf_size = reg_data & E1000_EXTCNF_SIZE_EXT_PCIE_LENGTH;
        cnf_size >>= 16;
        if (cnf_size) {
            reg_data = E1000_READ_REG(hw, EXTCNF_CTRL);
            cnf_base_addr = reg_data & E1000_EXTCNF_CTRL_EXT_CNF_POINTER;
            /* cnf_base_addr is in DWORD */
            cnf_base_addr >>= 16;

            /* Configure LCD from extended configuration region. */
            ret_val = e1000_init_lcd_from_nvm_config_region(hw, cnf_base_addr,
                                                            cnf_size);
            if (ret_val)
                return ret_val;
        }
    }

    return E1000_SUCCESS;
}


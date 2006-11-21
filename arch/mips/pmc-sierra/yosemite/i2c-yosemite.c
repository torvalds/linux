/*
 *  Copyright (C) 2003 PMC-Sierra Inc.
 *  Author: Manish Lachwani (lachwani@pmc-sierra.com)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Detailed Description:
 *
 * This block implements the I2C interface to the slave devices like the
 * Atmel 24C32 EEPROM and the MAX 1619 Sensors device. The I2C Master interface
 * can be controlled by the SCMB block. And the SCMB block kicks in only when
 * using the Ethernet Mode of operation and __not__ the SysAD mode
 *
 * The SCMB controls the two modes: MDIO and the I2C. The MDIO mode is used to
 * communicate with the Quad-PHY from Marvel. The I2C is used to communicate
 * with the I2C slave devices.  It seems that the driver does not explicitly
 * deal with the control of SDA and SCL serial lines. So, the driver will set
 * the slave address, drive the command and then the data.  The SCMB will then
 * control the two serial lines as required.
 *
 * It seems the documents are very unclear abt this. Hence, I took some time
 * out to write the desciption to have an idea of how the I2C can actually
 * work. Currently, this Linux driver wont be integrated into the generic Linux
 * I2C framework. And finally, the I2C interface is also known as the 2BI
 * interface. 2BI means 2-bit interface referring to SDA and SCL serial lines
 * respectively.
 *
 * - Manish Lachwani (12/09/2003)
 */

#include "i2c-yosemite.h"

/*
 * Poll the I2C interface for the BUSY bit.
 */
static int titan_i2c_poll(void)
{
	int i = 0;
	unsigned long val = 0;

	for (i = 0; i < TITAN_I2C_MAX_POLL; i++) {
		val = TITAN_I2C_READ(TITAN_I2C_COMMAND);

		if (!(val & 0x8000))
			return 0;
	}

	return TITAN_I2C_ERR_TIMEOUT;
}

/*
 * Execute the I2C command
 */
int titan_i2c_xfer(unsigned int slave_addr, titan_i2c_command * cmd,
		   int size, unsigned int *addr)
{
	int loop, bytes = 0, i;
	unsigned int *write_data, data, *read_data;
	unsigned long reg_val, val;

	write_data = cmd->data;
	read_data = addr;

	TITAN_I2C_WRITE(TITAN_I2C_SLAVE_ADDRESS, slave_addr);

	if (cmd->type == TITAN_I2C_CMD_WRITE)
		loop = cmd->write_size;
	else
		loop = size;

	while (loop > 0) {
		if ((cmd->type == TITAN_I2C_CMD_WRITE) ||
		    (cmd->type == TITAN_I2C_CMD_READ_WRITE)) {

			reg_val = TITAN_I2C_DATA;
			for (i = 0; i < TITAN_I2C_MAX_WORDS_PER_RW;
			     ++i, write_data += 2, reg_val += 4) {
				if (bytes < cmd->write_size) {
					data = write_data[0];
					++data;
				}

				if (bytes < cmd->write_size) {
					data = write_data[1];
					++data;
				}

				TITAN_I2C_WRITE(reg_val, data);
			}
		}

		TITAN_I2C_WRITE(TITAN_I2C_COMMAND,
				(unsigned int) (cmd->type << 13));
		if (titan_i2c_poll() != TITAN_I2C_ERR_OK)
			return TITAN_I2C_ERR_TIMEOUT;

		if ((cmd->type == TITAN_I2C_CMD_READ) ||
		    (cmd->type == TITAN_I2C_CMD_READ_WRITE)) {

			reg_val = TITAN_I2C_DATA;
			for (i = 0; i < TITAN_I2C_MAX_WORDS_PER_RW;
			     ++i, read_data += 2, reg_val += 4) {
				data = TITAN_I2C_READ(reg_val);

				if (bytes < size) {
					read_data[0] = data & 0xff;
					++bytes;
				}

				if (bytes < size) {
					read_data[1] =
					    ((data >> 8) & 0xff);
					++bytes;
				}
			}
		}

		loop -= (TITAN_I2C_MAX_WORDS_PER_RW * 2);
	}

	/*
	 * Read the Interrupt status and then return the appropriate error code
	 */

	val = TITAN_I2C_READ(TITAN_I2C_INTERRUPTS);
	if (val & 0x0020)
		return TITAN_I2C_ERR_ARB_LOST;

	if (val & 0x0040)
		return TITAN_I2C_ERR_NO_RESP;

	if (val & 0x0080)
		return TITAN_I2C_ERR_DATA_COLLISION;

	return TITAN_I2C_ERR_OK;
}

/*
 * Init the I2C subsystem of the PMC-Sierra Yosemite board
 */
int titan_i2c_init(titan_i2c_config * config)
{
	unsigned int val;

	/*
	 * Reset the SCMB and program into the I2C mode
	 */
	TITAN_I2C_WRITE(TITAN_I2C_SCMB_CONTROL, 0xA000);
	TITAN_I2C_WRITE(TITAN_I2C_SCMB_CONTROL, 0x2000);

	/*
	 * Configure the filtera and clka values
	 */
	val = TITAN_I2C_READ(TITAN_I2C_SCMB_CLOCK_A);
	val |= ((val & ~(0xF000)) | ((config->filtera << 12) & 0xF000));
	val |= ((val & ~(0x03FF)) | (config->clka & 0x03FF));
	TITAN_I2C_WRITE(TITAN_I2C_SCMB_CLOCK_A, val);

	/*
	 * Configure the filterb and clkb values
	 */
	val = TITAN_I2C_READ(TITAN_I2C_SCMB_CLOCK_B);
	val |= ((val & ~(0xF000)) | ((config->filterb << 12) & 0xF000));
	val |= ((val & ~(0x03FF)) | (config->clkb & 0x03FF));
	TITAN_I2C_WRITE(TITAN_I2C_SCMB_CLOCK_B, val);

	return TITAN_I2C_ERR_OK;
}

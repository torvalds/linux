/*

    cx88-vp3054-i2c.h  --  support for the secondary I2C bus of the
			   DNTV Live! DVB-T Pro (VP-3054), wired as:
			   GPIO[0] -> SCL, GPIO[1] -> SDA

    (c) 2005 Chris Pascoe <c.pascoe@itee.uq.edu.au>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/* ----------------------------------------------------------------------- */
struct vp3054_i2c_state {
	struct i2c_adapter         adap;
	struct i2c_algo_bit_data   algo;
	u32                        state;
};

/* ----------------------------------------------------------------------- */
int  vp3054_i2c_probe(struct cx8802_dev *dev);
void vp3054_i2c_remove(struct cx8802_dev *dev);

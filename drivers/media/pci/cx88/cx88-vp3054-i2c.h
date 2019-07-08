/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cx88-vp3054-i2c.h  --  support for the secondary I2C bus of the
 *			  DNTV Live! DVB-T Pro (VP-3054), wired as:
 *			  GPIO[0] -> SCL, GPIO[1] -> SDA
 *
 * (c) 2005 Chris Pascoe <c.pascoe@itee.uq.edu.au>
 */

/* ----------------------------------------------------------------------- */
struct vp3054_i2c_state {
	struct i2c_adapter         adap;
	struct i2c_algo_bit_data   algo;
	u32                        state;
};

/* ----------------------------------------------------------------------- */
#if IS_ENABLED(CONFIG_VIDEO_CX88_VP3054)
int  vp3054_i2c_probe(struct cx8802_dev *dev);
void vp3054_i2c_remove(struct cx8802_dev *dev);
#else
static inline int  vp3054_i2c_probe(struct cx8802_dev *dev)
{ return 0; }
static inline void vp3054_i2c_remove(struct cx8802_dev *dev)
{ }
#endif

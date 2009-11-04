/*
	Copyright (C) 2009 Bartlomiej Zolnierkiewicz

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef RT2800LIB_H
#define RT2800LIB_H

struct rt2800_ops {
	void (*register_read)(struct rt2x00_dev *rt2x00dev,
			      const unsigned int offset, u32 *value);
	void (*register_write)(struct rt2x00_dev *rt2x00dev,
			       const unsigned int offset, u32 value);
	void (*register_write_lock)(struct rt2x00_dev *rt2x00dev,
				    const unsigned int offset, u32 value);

	void (*register_multiread)(struct rt2x00_dev *rt2x00dev,
				   const unsigned int offset,
				   void *value, const u32 length);
	void (*register_multiwrite)(struct rt2x00_dev *rt2x00dev,
				    const unsigned int offset,
				    const void *value, const u32 length);

	int (*regbusy_read)(struct rt2x00_dev *rt2x00dev,
			    const unsigned int offset,
			    const struct rt2x00_field32 field, u32 *reg);
};

static inline void rt2800_register_read(struct rt2x00_dev *rt2x00dev,
					const unsigned int offset,
					u32 *value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_read(rt2x00dev, offset, value);
}

static inline void rt2800_register_write(struct rt2x00_dev *rt2x00dev,
					 const unsigned int offset,
					 u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_write(rt2x00dev, offset, value);
}

static inline void rt2800_register_write_lock(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_write_lock(rt2x00dev, offset, value);
}

static inline void rt2800_register_multiread(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     void *value, const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_multiread(rt2x00dev, offset, value, length);
}

static inline void rt2800_register_multiwrite(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      const void *value,
					      const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_multiwrite(rt2x00dev, offset, value, length);
}

static inline int rt2800_regbusy_read(struct rt2x00_dev *rt2x00dev,
				      const unsigned int offset,
				      const struct rt2x00_field32 field,
				      u32 *reg)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	return rt2800ops->regbusy_read(rt2x00dev, offset, field, reg);
}

#endif /* RT2800LIB_H */

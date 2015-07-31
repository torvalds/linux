/*
 * Copyright (C) STMicroelectronics SA 2013
 * Authors: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_CURSOR_H_
#define _STI_CURSOR_H_

struct sti_plane *sti_cursor_create(struct device *dev, int desc,
				    void __iomem *baseaddr);

#endif

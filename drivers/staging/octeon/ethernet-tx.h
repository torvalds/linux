/*
 * This file is based on code from OCTEON SDK by Cavium Networks.
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

int cvm_oct_xmit(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_xmit_pow(struct sk_buff *skb, struct net_device *dev);
int cvm_oct_transmit_qos(struct net_device *dev, void *work_queue_entry,
			 int do_free, int qos);
void cvm_oct_tx_initialize(void);
void cvm_oct_tx_shutdown(void);
void cvm_oct_tx_shutdown_dev(struct net_device *dev);

/*
 * Random number generator
 * Copyright (c) 2010-2011, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef RANDOM_H
#define RANDOM_H

#ifdef CONFIG_NO_RANDOM_POOL
#define random_init() do { } while (0)
#define random_deinit() do { } while (0)
#define random_add_randomness(b, l) do { } while (0)
#define random_get_bytes(b, l) os_get_random((b), (l))
#define random_pool_ready() 1
#define random_mark_pool_ready() do { } while (0)
#else /* CONFIG_NO_RANDOM_POOL */
void random_init(void);
void random_deinit(void);
void random_add_randomness(const void *buf, size_t len);
int random_get_bytes(void *buf, size_t len);
int random_pool_ready(void);
void random_mark_pool_ready(void);
#endif /* CONFIG_NO_RANDOM_POOL */

#endif /* RANDOM_H */

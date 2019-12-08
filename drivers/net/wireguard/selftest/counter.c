// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifdef DEBUG
bool __init wg_packet_counter_selftest(void)
{
	unsigned int test_num = 0, i;
	union noise_counter counter;
	bool success = true;

#define T_INIT do {                                               \
		memset(&counter, 0, sizeof(union noise_counter)); \
		spin_lock_init(&counter.receive.lock);            \
	} while (0)
#define T_LIM (COUNTER_WINDOW_SIZE + 1)
#define T(n, v) do {                                                  \
		++test_num;                                           \
		if (counter_validate(&counter, n) != (v)) {           \
			pr_err("nonce counter self-test %u: FAIL\n",  \
			       test_num);                             \
			success = false;                              \
		}                                                     \
	} while (0)

	T_INIT;
	/*  1 */ T(0, true);
	/*  2 */ T(1, true);
	/*  3 */ T(1, false);
	/*  4 */ T(9, true);
	/*  5 */ T(8, true);
	/*  6 */ T(7, true);
	/*  7 */ T(7, false);
	/*  8 */ T(T_LIM, true);
	/*  9 */ T(T_LIM - 1, true);
	/* 10 */ T(T_LIM - 1, false);
	/* 11 */ T(T_LIM - 2, true);
	/* 12 */ T(2, true);
	/* 13 */ T(2, false);
	/* 14 */ T(T_LIM + 16, true);
	/* 15 */ T(3, false);
	/* 16 */ T(T_LIM + 16, false);
	/* 17 */ T(T_LIM * 4, true);
	/* 18 */ T(T_LIM * 4 - (T_LIM - 1), true);
	/* 19 */ T(10, false);
	/* 20 */ T(T_LIM * 4 - T_LIM, false);
	/* 21 */ T(T_LIM * 4 - (T_LIM + 1), false);
	/* 22 */ T(T_LIM * 4 - (T_LIM - 2), true);
	/* 23 */ T(T_LIM * 4 + 1 - T_LIM, false);
	/* 24 */ T(0, false);
	/* 25 */ T(REJECT_AFTER_MESSAGES, false);
	/* 26 */ T(REJECT_AFTER_MESSAGES - 1, true);
	/* 27 */ T(REJECT_AFTER_MESSAGES, false);
	/* 28 */ T(REJECT_AFTER_MESSAGES - 1, false);
	/* 29 */ T(REJECT_AFTER_MESSAGES - 2, true);
	/* 30 */ T(REJECT_AFTER_MESSAGES + 1, false);
	/* 31 */ T(REJECT_AFTER_MESSAGES + 2, false);
	/* 32 */ T(REJECT_AFTER_MESSAGES - 2, false);
	/* 33 */ T(REJECT_AFTER_MESSAGES - 3, true);
	/* 34 */ T(0, false);

	T_INIT;
	for (i = 1; i <= COUNTER_WINDOW_SIZE; ++i)
		T(i, true);
	T(0, true);
	T(0, false);

	T_INIT;
	for (i = 2; i <= COUNTER_WINDOW_SIZE + 1; ++i)
		T(i, true);
	T(1, true);
	T(0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 0;)
		T(i, true);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 2; i-- > 1;)
		T(i, true);
	T(0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(i, true);
	T(COUNTER_WINDOW_SIZE + 1, true);
	T(0, false);

	T_INIT;
	for (i = COUNTER_WINDOW_SIZE + 1; i-- > 1;)
		T(i, true);
	T(0, true);
	T(COUNTER_WINDOW_SIZE + 1, true);

#undef T
#undef T_LIM
#undef T_INIT

	if (success)
		pr_info("nonce counter self-tests: pass\n");
	return success;
}
#endif

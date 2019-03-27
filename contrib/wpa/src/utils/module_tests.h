/*
 * Module tests
 * Copyright (c) 2014-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef MODULE_TESTS_H
#define MODULE_TESTS_H

int wpas_module_tests(void);
int hapd_module_tests(void);

int utils_module_tests(void);
int wps_module_tests(void);
int common_module_tests(void);
int crypto_module_tests(void);

#endif /* MODULE_TESTS_H */

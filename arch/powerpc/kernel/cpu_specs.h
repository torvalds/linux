/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef CONFIG_PPC_47x
#include "cpu_specs_47x.h"
#elif defined(CONFIG_44x)
#include "cpu_specs_44x.h"
#endif

#ifdef CONFIG_PPC_8xx
#include "cpu_specs_8xx.h"
#endif

#ifdef CONFIG_PPC_E500MC
#include "cpu_specs_e500mc.h"
#elif defined(CONFIG_PPC_85xx)
#include "cpu_specs_85xx.h"
#endif

#ifdef CONFIG_PPC_BOOK3S_32
#include "cpu_specs_book3s_32.h"
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#include "cpu_specs_book3s_64.h"
#endif

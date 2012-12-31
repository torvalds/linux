/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#include <osk/mali_osk.h>
#include <uk/mali_ukk.h>
#include <plat/mali_ukk_os.h>

/* following OS specific includes can be removed when osk functions are implemented */
#if 0
#include <linux/module.h>       /* Kernel module definitions */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#endif

mali_error ukk_start(void)
{
	return MALI_ERROR_NONE;
}

void ukk_stop(void)
{
}

void ukk_call_prepare(ukk_call_context * const ukk_ctx, ukk_session * const session, void * const thread_ctx)
{
	OSK_ASSERT(NULL != ukk_ctx);
	OSK_ASSERT(NULL != session);
	/* NULL thread_ctx parameter is allowed */

	ukk_ctx->ukk_session = session;
	ukk_ctx->thread_ctx = thread_ctx;
	ukk_ctx->data = 0;
}

void *ukk_session_get(ukk_call_context * const ukk_ctx)
{
	OSK_ASSERT(NULL != ukk_ctx);
	return ukk_ctx->ukk_session;
}

void ukk_call_data_set(ukk_call_context * const ukk_ctx, uintptr_t data)
{
	OSK_ASSERT(NULL != ukk_ctx);
	ukk_ctx->data = data;
}

uintptr_t ukk_call_data_get(ukk_call_context * const ukk_ctx)
{
	OSK_ASSERT(NULL != ukk_ctx);
	return ukk_ctx->data;
}

void *ukk_thread_ctx_get(ukk_call_context * const ukk_ctx)
{
	OSK_ASSERT(NULL != ukk_ctx);
	return ukk_ctx->thread_ctx;
}

static mali_error ukkp_dispatch_call(ukk_call_context *ukk_ctx, void *args, u32 args_size)
{
	uk_header *header = (uk_header *)args;
	mali_error ret = MALI_ERROR_NONE;

	if(UKP_FUNC_ID_CHECK_VERSION == header->id)
	{
		if (args_size == sizeof(uku_version_check_args))
		{
			ukk_session *ukk_session = ukk_session_get(ukk_ctx);
			uku_version_check_args *version_check = (uku_version_check_args *)args;

			version_check->major = ukk_session->version_major;
			version_check->minor = ukk_session->version_minor;
			header->ret = MALI_ERROR_NONE;
		}
		else
		{
			header->ret = MALI_ERROR_FUNCTION_FAILED;
		}
	}
	else
	{
		ret = MALI_ERROR_FUNCTION_FAILED; /* not handled */
        }
	return ret;
}

mali_error ukk_dispatch(ukk_call_context * const ukk_ctx, void * const args, u32 args_size)
{
	mali_error ret;
	uk_header *header = (uk_header *)args;

	OSK_ASSERT(NULL != ukk_ctx);
	OSK_ASSERT(NULL != args);

	/* Verify args_size both in debug and release builds */ 
	OSK_ASSERT(args_size >= sizeof(uk_header));
	if (args_size < sizeof(uk_header)) return MALI_ERROR_FUNCTION_FAILED;

	if (header->id >= UK_FUNC_ID)
	{
		ret = ukk_ctx->ukk_session->dispatch(ukk_ctx, args, args_size);
	}
	else
	{
		ret = ukkp_dispatch_call(ukk_ctx, args, args_size);
	}
	return ret;
}

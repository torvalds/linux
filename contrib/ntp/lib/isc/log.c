/*
 * Copyright (C) 2004-2007, 2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file
 * \author  Principal Authors: DCL */

#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#include <sys/types.h>	/* dev_t FreeBSD 2.1 */

#include <isc/dir.h>
#include <isc/file.h>
#include <isc/log.h>
#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/msgs.h>
#include <isc/print.h>
#include <isc/stat.h>
#include <isc/stdio.h>
#include <isc/string.h>
#include <isc/time.h>
#include <isc/util.h>
#include "ntp_stdlib.h"		/* NTP change for strlcpy, strlcat */

#define LCTX_MAGIC		ISC_MAGIC('L', 'c', 't', 'x')
#define VALID_CONTEXT(lctx)	ISC_MAGIC_VALID(lctx, LCTX_MAGIC)

#define LCFG_MAGIC		ISC_MAGIC('L', 'c', 'f', 'g')
#define VALID_CONFIG(lcfg)	ISC_MAGIC_VALID(lcfg, LCFG_MAGIC)

/*
 * XXXDCL make dynamic?
 */
#define LOG_BUFFER_SIZE	(8 * 1024)

#ifndef PATH_MAX
#define PATH_MAX 1024	/* AIX and others don't define this. */
#endif

/*!
 * This is the structure that holds each named channel.  A simple linked
 * list chains all of the channels together, so an individual channel is
 * found by doing strcmp()s with the names down the list.  Their should
 * be no performance penalty from this as it is expected that the number
 * of named channels will be no more than a dozen or so, and name lookups
 * from the head of the list are only done when isc_log_usechannel() is
 * called, which should also be very infrequent.
 */
typedef struct isc_logchannel isc_logchannel_t;

struct isc_logchannel {
	char *				name;
	unsigned int			type;
	int 				level;
	unsigned int			flags;
	isc_logdestination_t 		destination;
	ISC_LINK(isc_logchannel_t)	link;
};

/*!
 * The logchannellist structure associates categories and modules with
 * channels.  First the appropriate channellist is found based on the
 * category, and then each structure in the linked list is checked for
 * a matching module.  It is expected that the number of channels
 * associated with any given category will be very short, no more than
 * three or four in the more unusual cases.
 */
typedef struct isc_logchannellist isc_logchannellist_t;

struct isc_logchannellist {
	const isc_logmodule_t *		module;
	isc_logchannel_t *		channel;
	ISC_LINK(isc_logchannellist_t)	link;
};

/*!
 * This structure is used to remember messages for pruning via
 * isc_log_[v]write1().
 */
typedef struct isc_logmessage isc_logmessage_t;

struct isc_logmessage {
	char *				text;
	isc_time_t			time;
	ISC_LINK(isc_logmessage_t)	link;
};

/*!
 * The isc_logconfig structure is used to store the configurable information
 * about where messages are actually supposed to be sent -- the information
 * that could changed based on some configuration file, as opposed to the
 * the category/module specification of isc_log_[v]write[1] that is compiled
 * into a program, or the debug_level which is dynamic state information.
 */
struct isc_logconfig {
	unsigned int			magic;
	isc_log_t *			lctx;
	ISC_LIST(isc_logchannel_t)	channels;
	ISC_LIST(isc_logchannellist_t) *channellists;
	unsigned int			channellist_count;
	unsigned int			duplicate_interval;
	int				highest_level;
	char *				tag;
	isc_boolean_t			dynamic;
};

/*!
 * This isc_log structure provides the context for the isc_log functions.
 * The log context locks itself in isc_log_doit, the internal backend to
 * isc_log_write.  The locking is necessary both to provide exclusive access
 * to the buffer into which the message is formatted and to guard against
 * competing threads trying to write to the same syslog resource.  (On
 * some systems, such as BSD/OS, stdio is thread safe but syslog is not.)
 * Unfortunately, the lock cannot guard against a _different_ logging
 * context in the same program competing for syslog's attention.  Thus
 * There Can Be Only One, but this is not enforced.
 * XXXDCL enforce it?
 *
 * Note that the category and module information is not locked.
 * This is because in the usual case, only one isc_log_t is ever created
 * in a program, and the category/module registration happens only once.
 * XXXDCL it might be wise to add more locking overall.
 */
struct isc_log {
	/* Not locked. */
	unsigned int			magic;
	isc_mem_t *			mctx;
	isc_logcategory_t *		categories;
	unsigned int			category_count;
	isc_logmodule_t *		modules;
	unsigned int			module_count;
	int				debug_level;
	isc_mutex_t			lock;
	/* Locked by isc_log lock. */
	isc_logconfig_t * 		logconfig;
	char 				buffer[LOG_BUFFER_SIZE];
	ISC_LIST(isc_logmessage_t)	messages;
};

/*!
 * Used when ISC_LOG_PRINTLEVEL is enabled for a channel.
 */
static const char *log_level_strings[] = {
	"debug",
	"info",
	"notice",
	"warning",
	"error",
	"critical"
};

/*!
 * Used to convert ISC_LOG_* priorities into syslog priorities.
 * XXXDCL This will need modification for NT.
 */
static const int syslog_map[] = {
	LOG_DEBUG,
	LOG_INFO,
	LOG_NOTICE,
	LOG_WARNING,
	LOG_ERR,
	LOG_CRIT
};

/*!
 * When adding new categories, a corresponding ISC_LOGCATEGORY_foo
 * definition needs to be added to <isc/log.h>.
 *
 * The default category is provided so that the internal default can
 * be overridden.  Since the default is always looked up as the first
 * channellist in the log context, it must come first in isc_categories[].
 */
LIBISC_EXTERNAL_DATA isc_logcategory_t isc_categories[] = {
	{ "default", 0 },	/* "default" must come first. */
	{ "general", 0 },
	{ NULL, 0 }
};

/*!
 * See above comment for categories on LIBISC_EXTERNAL_DATA, and apply it to modules.
 */
LIBISC_EXTERNAL_DATA isc_logmodule_t isc_modules[] = {
	{ "socket", 0 },
	{ "time", 0 },
	{ "interface", 0 },
	{ "timer", 0 },
	{ "file", 0 },
	{ NULL, 0 }
};

/*!
 * This essentially constant structure must be filled in at run time,
 * because its channel member is pointed to a channel that is created
 * dynamically with isc_log_createchannel.
 */
static isc_logchannellist_t default_channel;

/*!
 * libisc logs to this context.
 */
LIBISC_EXTERNAL_DATA isc_log_t *isc_lctx = NULL;

/*!
 * Forward declarations.
 */
static isc_result_t
assignchannel(isc_logconfig_t *lcfg, unsigned int category_id,
	      const isc_logmodule_t *module, isc_logchannel_t *channel);

static isc_result_t
sync_channellist(isc_logconfig_t *lcfg);

static isc_result_t
greatest_version(isc_logchannel_t *channel, int *greatest);

static isc_result_t
roll_log(isc_logchannel_t *channel);

static void
isc_log_doit(isc_log_t *lctx, isc_logcategory_t *category,
	     isc_logmodule_t *module, int level, isc_boolean_t write_once,
	     isc_msgcat_t *msgcat, int msgset, int msg,
	     const char *format, va_list args)
     ISC_FORMAT_PRINTF(9, 0);

/*@{*/
/*!
 * Convenience macros.
 */

#define FACILITY(channel)	 (channel->destination.facility)
#define FILE_NAME(channel)	 (channel->destination.file.name)
#define FILE_STREAM(channel)	 (channel->destination.file.stream)
#define FILE_VERSIONS(channel)	 (channel->destination.file.versions)
#define FILE_MAXSIZE(channel)	 (channel->destination.file.maximum_size)
#define FILE_MAXREACHED(channel) (channel->destination.file.maximum_reached)

/*@}*/
/****
 **** Public interfaces.
 ****/

/*
 * Establish a new logging context, with default channels.
 */
isc_result_t
isc_log_create(isc_mem_t *mctx, isc_log_t **lctxp, isc_logconfig_t **lcfgp) {
	isc_log_t *lctx;
	isc_logconfig_t *lcfg = NULL;
	isc_result_t result;

	REQUIRE(mctx != NULL);
	REQUIRE(lctxp != NULL && *lctxp == NULL);
	REQUIRE(lcfgp == NULL || *lcfgp == NULL);

	lctx = isc_mem_get(mctx, sizeof(*lctx));
	if (lctx != NULL) {
		lctx->mctx = mctx;
		lctx->categories = NULL;
		lctx->category_count = 0;
		lctx->modules = NULL;
		lctx->module_count = 0;
		lctx->debug_level = 0;

		ISC_LIST_INIT(lctx->messages);

		result = isc_mutex_init(&lctx->lock);
		if (result != ISC_R_SUCCESS) {
			isc_mem_put(mctx, lctx, sizeof(*lctx));
			return (result);
		}

		/*
		 * Normally setting the magic number is the last step done
		 * in a creation function, but a valid log context is needed
		 * by isc_log_registercategories and isc_logconfig_create.
		 * If either fails, the lctx is destroyed and not returned
		 * to the caller.
		 */
		lctx->magic = LCTX_MAGIC;

		isc_log_registercategories(lctx, isc_categories);
		isc_log_registermodules(lctx, isc_modules);
		result = isc_logconfig_create(lctx, &lcfg);

	} else
		result = ISC_R_NOMEMORY;

	if (result == ISC_R_SUCCESS)
		result = sync_channellist(lcfg);

	if (result == ISC_R_SUCCESS) {
		lctx->logconfig = lcfg;

		*lctxp = lctx;
		if (lcfgp != NULL)
			*lcfgp = lcfg;

	} else {
		if (lcfg != NULL)
			isc_logconfig_destroy(&lcfg);
		if (lctx != NULL)
			isc_log_destroy(&lctx);
	}

	return (result);
}

isc_result_t
isc_logconfig_create(isc_log_t *lctx, isc_logconfig_t **lcfgp) {
	isc_logconfig_t *lcfg;
	isc_logdestination_t destination;
	isc_result_t result = ISC_R_SUCCESS;
	int level = ISC_LOG_INFO;

	REQUIRE(lcfgp != NULL && *lcfgp == NULL);
	REQUIRE(VALID_CONTEXT(lctx));

	lcfg = isc_mem_get(lctx->mctx, sizeof(*lcfg));

	if (lcfg != NULL) {
		lcfg->lctx = lctx;
		lcfg->channellists = NULL;
		lcfg->channellist_count = 0;
		lcfg->duplicate_interval = 0;
		lcfg->highest_level = level;
		lcfg->tag = NULL;
		lcfg->dynamic = ISC_FALSE;

		ISC_LIST_INIT(lcfg->channels);

		/*
		 * Normally the magic number is the last thing set in the
		 * structure, but isc_log_createchannel() needs a valid
		 * config.  If the channel creation fails, the lcfg is not
		 * returned to the caller.
		 */
		lcfg->magic = LCFG_MAGIC;

	} else
		result = ISC_R_NOMEMORY;

	/*
	 * Create the default channels:
	 *   	default_syslog, default_stderr, default_debug and null.
	 */
	if (result == ISC_R_SUCCESS) {
		destination.facility = LOG_DAEMON;
		result = isc_log_createchannel(lcfg, "default_syslog",
					       ISC_LOG_TOSYSLOG, level,
					       &destination, 0);
	}

	if (result == ISC_R_SUCCESS) {
		destination.file.stream = stderr;
		destination.file.name = NULL;
		destination.file.versions = ISC_LOG_ROLLNEVER;
		destination.file.maximum_size = 0;
		result = isc_log_createchannel(lcfg, "default_stderr",
					       ISC_LOG_TOFILEDESC,
					       level,
					       &destination,
					       ISC_LOG_PRINTTIME);
	}

	if (result == ISC_R_SUCCESS) {
		/*
		 * Set the default category's channel to default_stderr,
		 * which is at the head of the channels list because it was
		 * just created.
		 */
		default_channel.channel = ISC_LIST_HEAD(lcfg->channels);

		destination.file.stream = stderr;
		destination.file.name = NULL;
		destination.file.versions = ISC_LOG_ROLLNEVER;
		destination.file.maximum_size = 0;
		result = isc_log_createchannel(lcfg, "default_debug",
					       ISC_LOG_TOFILEDESC,
					       ISC_LOG_DYNAMIC,
					       &destination,
					       ISC_LOG_PRINTTIME);
	}

	if (result == ISC_R_SUCCESS)
		result = isc_log_createchannel(lcfg, "null",
					       ISC_LOG_TONULL,
					       ISC_LOG_DYNAMIC,
					       NULL, 0);

	if (result == ISC_R_SUCCESS)
		*lcfgp = lcfg;

	else
		if (lcfg != NULL)
			isc_logconfig_destroy(&lcfg);

	return (result);
}

isc_logconfig_t *
isc_logconfig_get(isc_log_t *lctx) {
	REQUIRE(VALID_CONTEXT(lctx));

	ENSURE(lctx->logconfig != NULL);

	return (lctx->logconfig);
}

isc_result_t
isc_logconfig_use(isc_log_t *lctx, isc_logconfig_t *lcfg) {
	isc_logconfig_t *old_cfg;
	isc_result_t result;

	REQUIRE(VALID_CONTEXT(lctx));
	REQUIRE(VALID_CONFIG(lcfg));
	REQUIRE(lcfg->lctx == lctx);

	/*
	 * Ensure that lcfg->channellist_count == lctx->category_count.
	 * They won't be equal if isc_log_usechannel has not been called
	 * since any call to isc_log_registercategories.
	 */
	result = sync_channellist(lcfg);
	if (result != ISC_R_SUCCESS)
		return (result);

	LOCK(&lctx->lock);

	old_cfg = lctx->logconfig;
	lctx->logconfig = lcfg;

	UNLOCK(&lctx->lock);

	isc_logconfig_destroy(&old_cfg);

	return (ISC_R_SUCCESS);
}

void
isc_log_destroy(isc_log_t **lctxp) {
	isc_log_t *lctx;
	isc_logconfig_t *lcfg;
	isc_mem_t *mctx;
	isc_logmessage_t *message;

	REQUIRE(lctxp != NULL && VALID_CONTEXT(*lctxp));

	lctx = *lctxp;
	mctx = lctx->mctx;

	if (lctx->logconfig != NULL) {
		lcfg = lctx->logconfig;
		lctx->logconfig = NULL;
		isc_logconfig_destroy(&lcfg);
	}

	DESTROYLOCK(&lctx->lock);

	while ((message = ISC_LIST_HEAD(lctx->messages)) != NULL) {
		ISC_LIST_UNLINK(lctx->messages, message, link);

		isc_mem_put(mctx, message,
			    sizeof(*message) + strlen(message->text) + 1);
	}

	lctx->buffer[0] = '\0';
	lctx->debug_level = 0;
	lctx->categories = NULL;
	lctx->category_count = 0;
	lctx->modules = NULL;
	lctx->module_count = 0;
	lctx->mctx = NULL;
	lctx->magic = 0;

	isc_mem_put(mctx, lctx, sizeof(*lctx));

	*lctxp = NULL;
}

void
isc_logconfig_destroy(isc_logconfig_t **lcfgp) {
	isc_logconfig_t *lcfg;
	isc_mem_t *mctx;
	isc_logchannel_t *channel;
	isc_logchannellist_t *item;
	char *filename;
	unsigned int i;

	REQUIRE(lcfgp != NULL && VALID_CONFIG(*lcfgp));

	lcfg = *lcfgp;

	/*
	 * This function cannot be called with a logconfig that is in
	 * use by a log context.
	 */
	REQUIRE(lcfg->lctx != NULL && lcfg->lctx->logconfig != lcfg);

	mctx = lcfg->lctx->mctx;

	while ((channel = ISC_LIST_HEAD(lcfg->channels)) != NULL) {
		ISC_LIST_UNLINK(lcfg->channels, channel, link);

		if (channel->type == ISC_LOG_TOFILE) {
			/*
			 * The filename for the channel may have ultimately
			 * started its life in user-land as a const string,
			 * but in isc_log_createchannel it gets copied
			 * into writable memory and is not longer truly const.
			 */
			DE_CONST(FILE_NAME(channel), filename);
			isc_mem_free(mctx, filename);

			if (FILE_STREAM(channel) != NULL)
				(void)fclose(FILE_STREAM(channel));
		}

		isc_mem_free(mctx, channel->name);
		isc_mem_put(mctx, channel, sizeof(*channel));
	}

	for (i = 0; i < lcfg->channellist_count; i++)
		while ((item = ISC_LIST_HEAD(lcfg->channellists[i])) != NULL) {
			ISC_LIST_UNLINK(lcfg->channellists[i], item, link);
			isc_mem_put(mctx, item, sizeof(*item));
		}

	if (lcfg->channellist_count > 0)
		isc_mem_put(mctx, lcfg->channellists,
			    lcfg->channellist_count *
			    sizeof(ISC_LIST(isc_logchannellist_t)));

	lcfg->dynamic = ISC_FALSE;
	if (lcfg->tag != NULL)
		isc_mem_free(lcfg->lctx->mctx, lcfg->tag);
	lcfg->tag = NULL;
	lcfg->highest_level = 0;
	lcfg->duplicate_interval = 0;
	lcfg->magic = 0;

	isc_mem_put(mctx, lcfg, sizeof(*lcfg));

	*lcfgp = NULL;
}

void
isc_log_registercategories(isc_log_t *lctx, isc_logcategory_t categories[]) {
	isc_logcategory_t *catp;

	REQUIRE(VALID_CONTEXT(lctx));
	REQUIRE(categories != NULL && categories[0].name != NULL);

	/*
	 * XXXDCL This somewhat sleazy situation of using the last pointer
	 * in one category array to point to the next array exists because
	 * this registration function returns void and I didn't want to have
	 * change everything that used it by making it return an isc_result_t.
	 * It would need to do that if it had to allocate memory to store
	 * pointers to each array passed in.
	 */
	if (lctx->categories == NULL)
		lctx->categories = categories;

	else {
		/*
		 * Adjust the last (NULL) pointer of the already registered
		 * categories to point to the incoming array.
		 */
		for (catp = lctx->categories; catp->name != NULL; )
			if (catp->id == UINT_MAX)
				/*
				 * The name pointer points to the next array.
				 * Ick.
				 */
				DE_CONST(catp->name, catp);
			else
				catp++;

		catp->name = (void *)categories;
		catp->id = UINT_MAX;
	}

	/*
	 * Update the id number of the category with its new global id.
	 */
	for (catp = categories; catp->name != NULL; catp++)
		catp->id = lctx->category_count++;
}

isc_logcategory_t *
isc_log_categorybyname(isc_log_t *lctx, const char *name) {
	isc_logcategory_t *catp;

	REQUIRE(VALID_CONTEXT(lctx));
	REQUIRE(name != NULL);

	for (catp = lctx->categories; catp->name != NULL; )
		if (catp->id == UINT_MAX)
			/*
			 * catp is neither modified nor returned to the
			 * caller, so removing its const qualifier is ok.
			 */
			DE_CONST(catp->name, catp);
		else {
			if (strcmp(catp->name, name) == 0)
				return (catp);
			catp++;
		}

	return (NULL);
}

void
isc_log_registermodules(isc_log_t *lctx, isc_logmodule_t modules[]) {
	isc_logmodule_t *modp;

	REQUIRE(VALID_CONTEXT(lctx));
	REQUIRE(modules != NULL && modules[0].name != NULL);

	/*
	 * XXXDCL This somewhat sleazy situation of using the last pointer
	 * in one category array to point to the next array exists because
	 * this registration function returns void and I didn't want to have
	 * change everything that used it by making it return an isc_result_t.
	 * It would need to do that if it had to allocate memory to store
	 * pointers to each array passed in.
	 */
	if (lctx->modules == NULL)
		lctx->modules = modules;

	else {
		/*
		 * Adjust the last (NULL) pointer of the already registered
		 * modules to point to the incoming array.
		 */
		for (modp = lctx->modules; modp->name != NULL; )
			if (modp->id == UINT_MAX)
				/*
				 * The name pointer points to the next array.
				 * Ick.
				 */
				DE_CONST(modp->name, modp);
			else
				modp++;

		modp->name = (void *)modules;
		modp->id = UINT_MAX;
	}

	/*
	 * Update the id number of the module with its new global id.
	 */
	for (modp = modules; modp->name != NULL; modp++)
		modp->id = lctx->module_count++;
}

isc_logmodule_t *
isc_log_modulebyname(isc_log_t *lctx, const char *name) {
	isc_logmodule_t *modp;

	REQUIRE(VALID_CONTEXT(lctx));
	REQUIRE(name != NULL);

	for (modp = lctx->modules; modp->name != NULL; )
		if (modp->id == UINT_MAX)
			/*
			 * modp is neither modified nor returned to the
			 * caller, so removing its const qualifier is ok.
			 */
			DE_CONST(modp->name, modp);
		else {
			if (strcmp(modp->name, name) == 0)
				return (modp);
			modp++;
		}

	return (NULL);
}

isc_result_t
isc_log_createchannel(isc_logconfig_t *lcfg, const char *name,
		      unsigned int type, int level,
		      const isc_logdestination_t *destination,
		      unsigned int flags)
{
	isc_logchannel_t *channel;
	isc_mem_t *mctx;

	REQUIRE(VALID_CONFIG(lcfg));
	REQUIRE(name != NULL);
	REQUIRE(type == ISC_LOG_TOSYSLOG   || type == ISC_LOG_TOFILE ||
		type == ISC_LOG_TOFILEDESC || type == ISC_LOG_TONULL);
	REQUIRE(destination != NULL || type == ISC_LOG_TONULL);
	REQUIRE(level >= ISC_LOG_CRITICAL);
	REQUIRE((flags &
		 (unsigned int)~(ISC_LOG_PRINTALL | ISC_LOG_DEBUGONLY)) == 0);

	/* XXXDCL find duplicate names? */

	mctx = lcfg->lctx->mctx;

	channel = isc_mem_get(mctx, sizeof(*channel));
	if (channel == NULL)
		return (ISC_R_NOMEMORY);

	channel->name = isc_mem_strdup(mctx, name);
	if (channel->name == NULL) {
		isc_mem_put(mctx, channel, sizeof(*channel));
		return (ISC_R_NOMEMORY);
	}

	channel->type = type;
	channel->level = level;
	channel->flags = flags;
	ISC_LINK_INIT(channel, link);

	switch (type) {
	case ISC_LOG_TOSYSLOG:
		FACILITY(channel) = destination->facility;
		break;

	case ISC_LOG_TOFILE:
		/*
		 * The file name is copied because greatest_version wants
		 * to scribble on it, so it needs to be definitely in
		 * writable memory.
		 */
		FILE_NAME(channel) =
			isc_mem_strdup(mctx, destination->file.name);
		FILE_STREAM(channel) = NULL;
		FILE_VERSIONS(channel) = destination->file.versions;
		FILE_MAXSIZE(channel) = destination->file.maximum_size;
		FILE_MAXREACHED(channel) = ISC_FALSE;
		break;

	case ISC_LOG_TOFILEDESC:
		FILE_NAME(channel) = NULL;
		FILE_STREAM(channel) = destination->file.stream;
		FILE_MAXSIZE(channel) = 0;
		FILE_VERSIONS(channel) = ISC_LOG_ROLLNEVER;
		break;

	case ISC_LOG_TONULL:
		/* Nothing. */
		break;

	default:
		isc_mem_put(mctx, channel->name, strlen(channel->name) + 1);
		isc_mem_put(mctx, channel, sizeof(*channel));
		return (ISC_R_UNEXPECTED);
	}

	ISC_LIST_PREPEND(lcfg->channels, channel, link);

	/*
	 * If default_stderr was redefined, make the default category
	 * point to the new default_stderr.
	 */
	if (strcmp(name, "default_stderr") == 0)
		default_channel.channel = channel;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_log_usechannel(isc_logconfig_t *lcfg, const char *name,
		   const isc_logcategory_t *category,
		   const isc_logmodule_t *module)
{
	isc_log_t *lctx;
	isc_logchannel_t *channel;
	isc_result_t result = ISC_R_SUCCESS;
	unsigned int i;

	REQUIRE(VALID_CONFIG(lcfg));
	REQUIRE(name != NULL);

	lctx = lcfg->lctx;

	REQUIRE(category == NULL || category->id < lctx->category_count);
	REQUIRE(module == NULL || module->id < lctx->module_count);

	for (channel = ISC_LIST_HEAD(lcfg->channels); channel != NULL;
	     channel = ISC_LIST_NEXT(channel, link))
		if (strcmp(name, channel->name) == 0)
			break;

	if (channel == NULL)
		return (ISC_R_NOTFOUND);

	if (category != NULL)
		result = assignchannel(lcfg, category->id, module, channel);

	else
		/*
		 * Assign to all categories.  Note that this includes
		 * the default channel.
		 */
		for (i = 0; i < lctx->category_count; i++) {
			result = assignchannel(lcfg, i, module, channel);
			if (result != ISC_R_SUCCESS)
				break;
		}

	return (result);
}

void
isc_log_write(isc_log_t *lctx, isc_logcategory_t *category,
	      isc_logmodule_t *module, int level, const char *format, ...)
{
	va_list args;

	/*
	 * Contract checking is done in isc_log_doit().
	 */

	va_start(args, format);
	isc_log_doit(lctx, category, module, level, ISC_FALSE,
		     NULL, 0, 0, format, args);
	va_end(args);
}

void
isc_log_vwrite(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level,
	       const char *format, va_list args)
{
	/*
	 * Contract checking is done in isc_log_doit().
	 */
	isc_log_doit(lctx, category, module, level, ISC_FALSE,
		     NULL, 0, 0, format, args);
}

void
isc_log_write1(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level, const char *format, ...)
{
	va_list args;

	/*
	 * Contract checking is done in isc_log_doit().
	 */

	va_start(args, format);
	isc_log_doit(lctx, category, module, level, ISC_TRUE,
		     NULL, 0, 0, format, args);
	va_end(args);
}

void
isc_log_vwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		isc_logmodule_t *module, int level,
		const char *format, va_list args)
{
	/*
	 * Contract checking is done in isc_log_doit().
	 */
	isc_log_doit(lctx, category, module, level, ISC_TRUE,
		     NULL, 0, 0, format, args);
}

void
isc_log_iwrite(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level,
	       isc_msgcat_t *msgcat, int msgset, int msg,
	       const char *format, ...)
{
	va_list args;

	/*
	 * Contract checking is done in isc_log_doit().
	 */

	va_start(args, format);
	isc_log_doit(lctx, category, module, level, ISC_FALSE,
		     msgcat, msgset, msg, format, args);
	va_end(args);
}

void
isc_log_ivwrite(isc_log_t *lctx, isc_logcategory_t *category,
	       isc_logmodule_t *module, int level,
	       isc_msgcat_t *msgcat, int msgset, int msg,
	       const char *format, va_list args)
{
	/*
	 * Contract checking is done in isc_log_doit().
	 */
	isc_log_doit(lctx, category, module, level, ISC_FALSE,
		     msgcat, msgset, msg, format, args);
}

void
isc_log_iwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		isc_logmodule_t *module, int level,
		isc_msgcat_t *msgcat, int msgset, int msg,
		const char *format, ...)
{
	va_list args;

	/*
	 * Contract checking is done in isc_log_doit().
	 */

	va_start(args, format);
	isc_log_doit(lctx, category, module, level, ISC_TRUE,
		     msgcat, msgset, msg, format, args);
	va_end(args);
}

void
isc_log_ivwrite1(isc_log_t *lctx, isc_logcategory_t *category,
		 isc_logmodule_t *module, int level,
		 isc_msgcat_t *msgcat, int msgset, int msg,
		 const char *format, va_list args)
{
	/*
	 * Contract checking is done in isc_log_doit().
	 */
	isc_log_doit(lctx, category, module, level, ISC_TRUE,
		     msgcat, msgset, msg, format, args);
}

void
isc_log_setcontext(isc_log_t *lctx) {
	isc_lctx = lctx;
}

void
isc_log_setdebuglevel(isc_log_t *lctx, unsigned int level) {
	isc_logchannel_t *channel;

	REQUIRE(VALID_CONTEXT(lctx));

	LOCK(&lctx->lock);

	lctx->debug_level = level;
	/*
	 * Close ISC_LOG_DEBUGONLY channels if level is zero.
	 */
	if (lctx->debug_level == 0)
		for (channel = ISC_LIST_HEAD(lctx->logconfig->channels);
		     channel != NULL;
		     channel = ISC_LIST_NEXT(channel, link))
			if (channel->type == ISC_LOG_TOFILE &&
			    (channel->flags & ISC_LOG_DEBUGONLY) != 0 &&
			    FILE_STREAM(channel) != NULL) {
				(void)fclose(FILE_STREAM(channel));
				FILE_STREAM(channel) = NULL;
			}
	UNLOCK(&lctx->lock);
}

unsigned int
isc_log_getdebuglevel(isc_log_t *lctx) {
	REQUIRE(VALID_CONTEXT(lctx));

	return (lctx->debug_level);
}

void
isc_log_setduplicateinterval(isc_logconfig_t *lcfg, unsigned int interval) {
	REQUIRE(VALID_CONFIG(lcfg));

	lcfg->duplicate_interval = interval;
}

unsigned int
isc_log_getduplicateinterval(isc_logconfig_t *lcfg) {
	REQUIRE(VALID_CONTEXT(lcfg));

	return (lcfg->duplicate_interval);
}

isc_result_t
isc_log_settag(isc_logconfig_t *lcfg, const char *tag) {
	REQUIRE(VALID_CONFIG(lcfg));

	if (tag != NULL && *tag != '\0') {
		if (lcfg->tag != NULL)
			isc_mem_free(lcfg->lctx->mctx, lcfg->tag);
		lcfg->tag = isc_mem_strdup(lcfg->lctx->mctx, tag);
		if (lcfg->tag == NULL)
			return (ISC_R_NOMEMORY);

	} else {
		if (lcfg->tag != NULL)
			isc_mem_free(lcfg->lctx->mctx, lcfg->tag);
		lcfg->tag = NULL;
	}

	return (ISC_R_SUCCESS);
}

char *
isc_log_gettag(isc_logconfig_t *lcfg) {
	REQUIRE(VALID_CONFIG(lcfg));

	return (lcfg->tag);
}

/* XXXDCL NT  -- This interface will assuredly be changing. */
void
isc_log_opensyslog(const char *tag, int options, int facility) {
	(void)openlog(tag, options, facility);
}

void
isc_log_closefilelogs(isc_log_t *lctx) {
	isc_logchannel_t *channel;

	REQUIRE(VALID_CONTEXT(lctx));

	LOCK(&lctx->lock);
	for (channel = ISC_LIST_HEAD(lctx->logconfig->channels);
	     channel != NULL;
	     channel = ISC_LIST_NEXT(channel, link))

		if (channel->type == ISC_LOG_TOFILE &&
		    FILE_STREAM(channel) != NULL) {
			(void)fclose(FILE_STREAM(channel));
			FILE_STREAM(channel) = NULL;
		}
	UNLOCK(&lctx->lock);
}

/****
 **** Internal functions
 ****/

static isc_result_t
assignchannel(isc_logconfig_t *lcfg, unsigned int category_id,
	      const isc_logmodule_t *module, isc_logchannel_t *channel)
{
	isc_logchannellist_t *new_item;
	isc_log_t *lctx;
	isc_result_t result;

	REQUIRE(VALID_CONFIG(lcfg));

	lctx = lcfg->lctx;

	REQUIRE(category_id < lctx->category_count);
	REQUIRE(module == NULL || module->id < lctx->module_count);
	REQUIRE(channel != NULL);

	/*
	 * Ensure lcfg->channellist_count == lctx->category_count.
	 */
	result = sync_channellist(lcfg);
	if (result != ISC_R_SUCCESS)
		return (result);

	new_item = isc_mem_get(lctx->mctx, sizeof(*new_item));
	if (new_item == NULL)
		return (ISC_R_NOMEMORY);

	new_item->channel = channel;
	new_item->module = module;
	ISC_LIST_INITANDPREPEND(lcfg->channellists[category_id],
			       new_item, link);

	/*
	 * Remember the highest logging level set by any channel in the
	 * logging config, so isc_log_doit() can quickly return if the
	 * message is too high to be logged by any channel.
	 */
	if (channel->type != ISC_LOG_TONULL) {
		if (lcfg->highest_level < channel->level)
			lcfg->highest_level = channel->level;
		if (channel->level == ISC_LOG_DYNAMIC)
			lcfg->dynamic = ISC_TRUE;
	}

	return (ISC_R_SUCCESS);
}

/*
 * This would ideally be part of isc_log_registercategories(), except then
 * that function would have to return isc_result_t instead of void.
 */
static isc_result_t
sync_channellist(isc_logconfig_t *lcfg) {
	unsigned int bytes;
	isc_log_t *lctx;
	void *lists;

	REQUIRE(VALID_CONFIG(lcfg));

	lctx = lcfg->lctx;

	REQUIRE(lctx->category_count != 0);

	if (lctx->category_count == lcfg->channellist_count)
		return (ISC_R_SUCCESS);

	bytes = lctx->category_count * sizeof(ISC_LIST(isc_logchannellist_t));

	lists = isc_mem_get(lctx->mctx, bytes);

	if (lists == NULL)
		return (ISC_R_NOMEMORY);

	memset(lists, 0, bytes);

	if (lcfg->channellist_count != 0) {
		bytes = lcfg->channellist_count *
			sizeof(ISC_LIST(isc_logchannellist_t));
		memcpy(lists, lcfg->channellists, bytes);
		isc_mem_put(lctx->mctx, lcfg->channellists, bytes);
	}

	lcfg->channellists = lists;
	lcfg->channellist_count = lctx->category_count;

	return (ISC_R_SUCCESS);
}

static isc_result_t
greatest_version(isc_logchannel_t *channel, int *greatestp) {
	/* XXXDCL HIGHLY NT */
	char *basenam, *digit_end;
	const char *dirname;
	int version, greatest = -1;
	size_t basenamelen;
	isc_dir_t dir;
	isc_result_t result;
	char sep = '/';
#ifdef _WIN32
	char *basename2;
#endif

	REQUIRE(channel->type == ISC_LOG_TOFILE);

	/*
	 * It is safe to DE_CONST the file.name because it was copied
	 * with isc_mem_strdup in isc_log_createchannel.
	 */
	basenam = strrchr(FILE_NAME(channel), sep);
#ifdef _WIN32
	basename2 = strrchr(FILE_NAME(channel), '\\');
	if ((basenam != NULL && basename2 != NULL && basename2 > basenam) ||
	    (basenam == NULL && basename2 != NULL)) {
		basenam = basename2;
		sep = '\\';
	}
#endif
	if (basenam != NULL) {
		*basenam++ = '\0';
		dirname = FILE_NAME(channel);
	} else {
		DE_CONST(FILE_NAME(channel), basenam);
		dirname = ".";
	}
	basenamelen = strlen(basenam);

	isc_dir_init(&dir);
	result = isc_dir_open(&dir, dirname);

	/*
	 * Replace the file separator if it was taken out.
	 */
	if (basenam != FILE_NAME(channel))
		*(basenam - 1) = sep;

	/*
	 * Return if the directory open failed.
	 */
	if (result != ISC_R_SUCCESS)
		return (result);

	while (isc_dir_read(&dir) == ISC_R_SUCCESS) {
		if (dir.entry.length > basenamelen &&
		    strncmp(dir.entry.name, basenam, basenamelen) == 0 &&
		    dir.entry.name[basenamelen] == '.') {

			version = strtol(&dir.entry.name[basenamelen + 1],
					 &digit_end, 10);
			if (*digit_end == '\0' && version > greatest)
				greatest = version;
		}
	}
	isc_dir_close(&dir);

	*greatestp = ++greatest;

	return (ISC_R_SUCCESS);
}

static isc_result_t
roll_log(isc_logchannel_t *channel) {
	int i, n, greatest;
	char current[PATH_MAX + 1];
	char new[PATH_MAX + 1];
	const char *path;
	isc_result_t result;

	/*
	 * Do nothing (not even excess version trimming) if ISC_LOG_ROLLNEVER
	 * is specified.  Apparently complete external control over the log
	 * files is desired.
	 */
	if (FILE_VERSIONS(channel) == ISC_LOG_ROLLNEVER)
		return (ISC_R_SUCCESS);

	path = FILE_NAME(channel);

	/*
	 * Set greatest_version to the greatest existing version
	 * (not the maximum requested version).  This is 1 based even
	 * though the file names are 0 based, so an oldest log of log.1
	 * is a greatest_version of 2.
	 */
	result = greatest_version(channel, &greatest);
	if (result != ISC_R_SUCCESS)
		return (result);

	/*
	 * Now greatest should be set to the highest version number desired.
	 * Since the highest number is one less than FILE_VERSIONS(channel)
	 * when not doing infinite log rolling, greatest will need to be
	 * decremented when it is equal to -- or greater than --
	 * FILE_VERSIONS(channel).  When greatest is less than
	 * FILE_VERSIONS(channel), it is already suitable for use as
	 * the maximum version number.
	 */

	if (FILE_VERSIONS(channel) == ISC_LOG_ROLLINFINITE ||
	    FILE_VERSIONS(channel) > greatest)
		;		/* Do nothing. */
	else
		/*
		 * When greatest is >= FILE_VERSIONS(channel), it needs to
		 * be reduced until it is FILE_VERSIONS(channel) - 1.
		 * Remove any excess logs on the way to that value.
		 */
		while (--greatest >= FILE_VERSIONS(channel)) {
			n = snprintf(current, sizeof(current), "%s.%d",
				     path, greatest);
			if (n >= (int)sizeof(current) || n < 0)
				result = ISC_R_NOSPACE;
			else
				result = isc_file_remove(current);
			if (result != ISC_R_SUCCESS &&
			    result != ISC_R_FILENOTFOUND)
				syslog(LOG_ERR,
				       "unable to remove log file '%s.%d': %s",
				       path, greatest,
				       isc_result_totext(result));
		}

	for (i = greatest; i > 0; i--) {
		result = ISC_R_SUCCESS;
		n = snprintf(current, sizeof(current), "%s.%d", path, i - 1);
		if (n >= (int)sizeof(current) || n < 0)
			result = ISC_R_NOSPACE;
		if (result == ISC_R_SUCCESS) {
			n = snprintf(new, sizeof(new), "%s.%d", path, i);
			if (n >= (int)sizeof(new) || n < 0)
				result = ISC_R_NOSPACE;
		}
		if (result == ISC_R_SUCCESS)
			result = isc_file_rename(current, new);
		if (result != ISC_R_SUCCESS &&
		    result != ISC_R_FILENOTFOUND)
			syslog(LOG_ERR,
			       "unable to rename log file '%s.%d' to "
			       "'%s.%d': %s", path, i - 1, path, i,
			       isc_result_totext(result));
	}

	if (FILE_VERSIONS(channel) != 0) {
		n = snprintf(new, sizeof(new), "%s.0", path);
		if (n >= (int)sizeof(new) || n < 0)
			result = ISC_R_NOSPACE;
		else
			result = isc_file_rename(path, new);
		if (result != ISC_R_SUCCESS &&
		    result != ISC_R_FILENOTFOUND)
			syslog(LOG_ERR,
			       "unable to rename log file '%s' to '%s.0': %s",
			       path, path, isc_result_totext(result));
	} else {
		result = isc_file_remove(path);
		if (result != ISC_R_SUCCESS &&
		    result != ISC_R_FILENOTFOUND)
			syslog(LOG_ERR, "unable to remove log file '%s': %s",
			       path, isc_result_totext(result));
	}

	return (ISC_R_SUCCESS);
}

static isc_result_t
isc_log_open(isc_logchannel_t *channel) {
	struct stat statbuf;
	isc_boolean_t regular_file;
	isc_boolean_t roll = ISC_FALSE;
	isc_result_t result = ISC_R_SUCCESS;
	const char *path;

	REQUIRE(channel->type == ISC_LOG_TOFILE);
	REQUIRE(FILE_STREAM(channel) == NULL);

	path = FILE_NAME(channel);

	REQUIRE(path != NULL && *path != '\0');

	/*
	 * Determine type of file; only regular files will be
	 * version renamed, and only if the base file exists
	 * and either has no size limit or has reached its size limit.
	 */
	if (stat(path, &statbuf) == 0) {
		regular_file = S_ISREG(statbuf.st_mode) ? ISC_TRUE : ISC_FALSE;
		/* XXXDCL if not regular_file complain? */
		if ((FILE_MAXSIZE(channel) == 0 &&
		     FILE_VERSIONS(channel) != ISC_LOG_ROLLNEVER) ||
		    (FILE_MAXSIZE(channel) > 0 &&
		     statbuf.st_size >= FILE_MAXSIZE(channel)))
			roll = regular_file;
	} else if (errno == ENOENT) {
		regular_file = ISC_TRUE;
		POST(regular_file);
	} else
		result = ISC_R_INVALIDFILE;

	/*
	 * Version control.
	 */
	if (result == ISC_R_SUCCESS && roll) {
		if (FILE_VERSIONS(channel) == ISC_LOG_ROLLNEVER)
			return (ISC_R_MAXSIZE);
		result = roll_log(channel);
		if (result != ISC_R_SUCCESS) {
			if ((channel->flags & ISC_LOG_OPENERR) == 0) {
				syslog(LOG_ERR,
				       "isc_log_open: roll_log '%s' "
				       "failed: %s",
				       FILE_NAME(channel),
				       isc_result_totext(result));
				channel->flags |= ISC_LOG_OPENERR;
			}
			return (result);
		}
	}

	result = isc_stdio_open(path, "a", &FILE_STREAM(channel));

	return (result);
}

isc_boolean_t
isc_log_wouldlog(isc_log_t *lctx, int level) {
	/*
	 * Try to avoid locking the mutex for messages which can't
	 * possibly be logged to any channels -- primarily debugging
	 * messages that the debug level is not high enough to print.
	 *
	 * If the level is (mathematically) less than or equal to the
	 * highest_level, or if there is a dynamic channel and the level is
	 * less than or equal to the debug level, the main loop must be
	 * entered to see if the message should really be output.
	 *
	 * NOTE: this is UNLOCKED access to the logconfig.  However,
	 * the worst thing that can happen is that a bad decision is made
	 * about returning without logging, and that's not a big concern,
	 * because that's a risk anyway if the logconfig is being
	 * dynamically changed.
	 */

	if (lctx == NULL || lctx->logconfig == NULL)
		return (ISC_FALSE);

	return (ISC_TF(level <= lctx->logconfig->highest_level ||
		       (lctx->logconfig->dynamic &&
			level <= lctx->debug_level)));
}

static void
isc_log_doit(isc_log_t *lctx, isc_logcategory_t *category,
	     isc_logmodule_t *module, int level, isc_boolean_t write_once,
	     isc_msgcat_t *msgcat, int msgset, int msg,
	     const char *format, va_list args)
{
	int syslog_level;
	char time_string[64];
	char level_string[24];
	size_t octets;
	const char *iformat;
	struct stat statbuf;
	isc_boolean_t matched = ISC_FALSE;
	isc_boolean_t printtime, printtag;
	isc_boolean_t printcategory, printmodule, printlevel;
	isc_logconfig_t *lcfg;
	isc_logchannel_t *channel;
	isc_logchannellist_t *category_channels;
	isc_result_t result;

	REQUIRE(lctx == NULL || VALID_CONTEXT(lctx));
	REQUIRE(category != NULL);
	REQUIRE(module != NULL);
	REQUIRE(level != ISC_LOG_DYNAMIC);
	REQUIRE(format != NULL);

	/*
	 * Programs can use libraries that use this logging code without
	 * wanting to do any logging, thus the log context is allowed to
	 * be non-existent.
	 */
	if (lctx == NULL)
		return;

	REQUIRE(category->id < lctx->category_count);
	REQUIRE(module->id < lctx->module_count);

	if (! isc_log_wouldlog(lctx, level))
		return;

	if (msgcat != NULL)
		iformat = isc_msgcat_get(msgcat, msgset, msg, format);
	else
		iformat = format;

	time_string[0]  = '\0';
	level_string[0] = '\0';

	LOCK(&lctx->lock);

	lctx->buffer[0] = '\0';

	lcfg = lctx->logconfig;

	category_channels = ISC_LIST_HEAD(lcfg->channellists[category->id]);

	/*
	 * XXXDCL add duplicate filtering? (To not write multiple times to
	 * the same source via various channels).
	 */
	do {
		/*
		 * If the channel list end was reached and a match was made,
		 * everything is finished.
		 */
		if (category_channels == NULL && matched)
			break;

		if (category_channels == NULL && ! matched &&
		    category_channels != ISC_LIST_HEAD(lcfg->channellists[0]))
			/*
			 * No category/module pair was explicitly configured.
			 * Try the category named "default".
			 */
			category_channels =
				ISC_LIST_HEAD(lcfg->channellists[0]);

		if (category_channels == NULL && ! matched)
			/*
			 * No matching module was explicitly configured
			 * for the category named "default".  Use the internal
			 * default channel.
			 */
			category_channels = &default_channel;

		if (category_channels->module != NULL &&
		    category_channels->module != module) {
			category_channels = ISC_LIST_NEXT(category_channels,
							  link);
			continue;
		}

		matched = ISC_TRUE;

		channel = category_channels->channel;
		category_channels = ISC_LIST_NEXT(category_channels, link);

		if (((channel->flags & ISC_LOG_DEBUGONLY) != 0) &&
		    lctx->debug_level == 0)
			continue;

		if (channel->level == ISC_LOG_DYNAMIC) {
			if (lctx->debug_level < level)
				continue;
		} else if (channel->level < level)
			continue;

		if ((channel->flags & ISC_LOG_PRINTTIME) != 0 &&
		    time_string[0] == '\0') {
			isc_time_t isctime;

			TIME_NOW(&isctime);
			isc_time_formattimestamp(&isctime, time_string,
						 sizeof(time_string));
		}

		if ((channel->flags & ISC_LOG_PRINTLEVEL) != 0 &&
		    level_string[0] == '\0') {
			if (level < ISC_LOG_CRITICAL)
				snprintf(level_string, sizeof(level_string),
					 "%s %d: ",
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_LOG,
							ISC_MSG_LEVEL,
							"level"),
					 level);
			else if (level > ISC_LOG_DYNAMIC)
				snprintf(level_string, sizeof(level_string),
					 "%s %d: ", log_level_strings[0],
					 level);
			else
				snprintf(level_string, sizeof(level_string),
					 "%s: ", log_level_strings[-level]);
		}

		/*
		 * Only format the message once.
		 */
		if (lctx->buffer[0] == '\0') {
			(void)vsnprintf(lctx->buffer, sizeof(lctx->buffer),
					iformat, args);

			/*
			 * Check for duplicates.
			 */
			if (write_once) {
				isc_logmessage_t *message, *new;
				isc_time_t oldest;
				isc_interval_t interval;

				isc_interval_set(&interval,
						 lcfg->duplicate_interval, 0);

				/*
				 * 'oldest' is the age of the oldest messages
				 * which fall within the duplicate_interval
				 * range.
				 */
				TIME_NOW(&oldest);
				if (isc_time_subtract(&oldest, &interval, &oldest)
				    != ISC_R_SUCCESS)
					/*
					 * Can't effectively do the checking
					 * without having a valid time.
					 */
					message = NULL;
				else
					message =ISC_LIST_HEAD(lctx->messages);

				while (message != NULL) {
					if (isc_time_compare(&message->time,
							     &oldest) < 0) {
						/*
						 * This message is older
						 * than the duplicate_interval,
						 * so it should be dropped from
						 * the history.
						 *
						 * Setting the interval to be
						 * to be longer will obviously
						 * not cause the expired
						 * message to spring back into
						 * existence.
						 */
						new = ISC_LIST_NEXT(message,
								    link);

						ISC_LIST_UNLINK(lctx->messages,
								message, link);

						isc_mem_put(lctx->mctx,
							message,
							sizeof(*message) + 1 +
							strlen(message->text));

						message = new;
						continue;
					}

					/*
					 * This message is in the duplicate
					 * filtering interval ...
					 */
					if (strcmp(lctx->buffer, message->text)
					    == 0) {
						/*
						 * ... and it is a duplicate.
						 * Unlock the mutex and
						 * get the hell out of Dodge.
						 */
						UNLOCK(&lctx->lock);
						return;
					}

					message = ISC_LIST_NEXT(message, link);
				}

				/*
				 * It wasn't in the duplicate interval,
				 * so add it to the message list.
				 */
				octets = strlen(lctx->buffer) + 1;
				new = isc_mem_get(lctx->mctx,
						  sizeof(isc_logmessage_t) +
						  octets);
				if (new != NULL) {
					/*
					 * Put the text immediately after
					 * the struct.  The strcpy is safe.
					 */
					new->text = (char *)(new + 1);
					strlcpy(new->text, lctx->buffer, octets);

					TIME_NOW(&new->time);

					ISC_LIST_APPEND(lctx->messages,
							new, link);
				}
			}
		}

		printtime     = ISC_TF((channel->flags & ISC_LOG_PRINTTIME)
				       != 0);
		printtag      = ISC_TF((channel->flags & ISC_LOG_PRINTTAG)
				       != 0 && lcfg->tag != NULL);
		printcategory = ISC_TF((channel->flags & ISC_LOG_PRINTCATEGORY)
				       != 0);
		printmodule   = ISC_TF((channel->flags & ISC_LOG_PRINTMODULE)
				       != 0);
		printlevel    = ISC_TF((channel->flags & ISC_LOG_PRINTLEVEL)
				       != 0);

		switch (channel->type) {
		case ISC_LOG_TOFILE:
			if (FILE_MAXREACHED(channel)) {
				/*
				 * If the file can be rolled, OR
				 * If the file no longer exists, OR
				 * If the file is less than the maximum size,
				 *    (such as if it had been renamed and
				 *     a new one touched, or it was truncated
				 *     in place)
				 * ... then close it to trigger reopening.
				 */
				if (FILE_VERSIONS(channel) !=
				    ISC_LOG_ROLLNEVER ||
				    (stat(FILE_NAME(channel), &statbuf) != 0 &&
				     errno == ENOENT) ||
				    statbuf.st_size < FILE_MAXSIZE(channel)) {
					(void)fclose(FILE_STREAM(channel));
					FILE_STREAM(channel) = NULL;
					FILE_MAXREACHED(channel) = ISC_FALSE;
				} else
					/*
					 * Eh, skip it.
					 */
					break;
			}

			if (FILE_STREAM(channel) == NULL) {
				result = isc_log_open(channel);
				if (result != ISC_R_SUCCESS &&
				    result != ISC_R_MAXSIZE &&
				    (channel->flags & ISC_LOG_OPENERR) == 0) {
					syslog(LOG_ERR,
					       "isc_log_open '%s' failed: %s",
					       FILE_NAME(channel),
					       isc_result_totext(result));
					channel->flags |= ISC_LOG_OPENERR;
				}
				if (result != ISC_R_SUCCESS)
					break;
				channel->flags &= ~ISC_LOG_OPENERR;
			}
			/* FALLTHROUGH */

		case ISC_LOG_TOFILEDESC:
			fprintf(FILE_STREAM(channel), "%s%s%s%s%s%s%s%s%s%s\n",
				printtime     ? time_string	: "",
				printtime     ? " "		: "",
				printtag      ? lcfg->tag	: "",
				printtag      ? ": "		: "",
				printcategory ? category->name	: "",
				printcategory ? ": "		: "",
				printmodule   ? (module != NULL ? module->name
								: "no_module")
								: "",
				printmodule   ? ": "		: "",
				printlevel    ? level_string	: "",
				lctx->buffer);

			fflush(FILE_STREAM(channel));

			/*
			 * If the file now exceeds its maximum size
			 * threshold, note it so that it will not be logged
			 * to any more.
			 */
			if (FILE_MAXSIZE(channel) > 0) {
				INSIST(channel->type == ISC_LOG_TOFILE);

				/* XXXDCL NT fstat/fileno */
				/* XXXDCL complain if fstat fails? */
				if (fstat(fileno(FILE_STREAM(channel)),
					  &statbuf) >= 0 &&
				    statbuf.st_size > FILE_MAXSIZE(channel))
					FILE_MAXREACHED(channel) = ISC_TRUE;
			}

			break;

		case ISC_LOG_TOSYSLOG:
			if (level > 0)
				syslog_level = LOG_DEBUG;
			else if (level < ISC_LOG_CRITICAL)
				syslog_level = LOG_CRIT;
			else
				syslog_level = syslog_map[-level];

			(void)syslog(FACILITY(channel) | syslog_level,
			       "%s%s%s%s%s%s%s%s%s%s",
			       printtime     ? time_string	: "",
			       printtime     ? " "		: "",
			       printtag      ? lcfg->tag	: "",
			       printtag      ? ": "		: "",
			       printcategory ? category->name	: "",
			       printcategory ? ": "		: "",
			       printmodule   ? (module != NULL	? module->name
								: "no_module")
								: "",
			       printmodule   ? ": "		: "",
			       printlevel    ? level_string	: "",
			       lctx->buffer);
			break;

		case ISC_LOG_TONULL:
			break;

		}

	} while (1);

	UNLOCK(&lctx->lock);
}

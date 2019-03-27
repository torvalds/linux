/*
 * Copyright (c) 2002 - 2005 NetGroup, Politecnico di Torino (Italy)
 * Copyright (c) 2005 - 2008 CACE Technologies, Davis (California)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino, CACE Technologies
 * nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * sockutils.h may include <crtdbg.h> on Windows, and pcap-int.h will
 * include portability.h, and portability.h, on Windows, expects that
 * <crtdbg.h> has already been included, so include sockutils.h first.
 */
#include "sockutils.h"
#include "pcap-int.h"	// for the details of the pcap_t structure
#include "pcap-rpcap.h"
#include "rpcap-protocol.h"
#include <errno.h>		// for the errno variable
#include <stdlib.h>		// for malloc(), free(), ...
#include <string.h>		// for strstr, etc

#ifndef _WIN32
#include <dirent.h>		// for readdir
#endif

/* String identifier to be used in the pcap_findalldevs_ex() */
#define PCAP_TEXT_SOURCE_FILE "File"
/* String identifier to be used in the pcap_findalldevs_ex() */
#define PCAP_TEXT_SOURCE_ADAPTER "Network adapter"

/* String identifier to be used in the pcap_findalldevs_ex() */
#define PCAP_TEXT_SOURCE_ON_LOCAL_HOST "on local host"

/****************************************************
 *                                                  *
 * Function bodies                                  *
 *                                                  *
 ****************************************************/

int pcap_findalldevs_ex(char *source, struct pcap_rmtauth *auth, pcap_if_t **alldevs, char *errbuf)
{
	int type;
	char name[PCAP_BUF_SIZE], path[PCAP_BUF_SIZE], filename[PCAP_BUF_SIZE];
	pcap_t *fp;
	char tmpstring[PCAP_BUF_SIZE + 1];		/* Needed to convert names and descriptions from 'old' syntax to the 'new' one */
	pcap_if_t *lastdev;	/* Last device in the pcap_if_t list */
	pcap_if_t *dev;		/* Device we're adding to the pcap_if_t list */

	/* List starts out empty. */
	(*alldevs) = NULL;
	lastdev = NULL;

	if (strlen(source) > PCAP_BUF_SIZE)
	{
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "The source string is too long. Cannot handle it correctly.");
		return -1;
	}

	/*
	 * Determine the type of the source (file, local, remote)
	 * There are some differences if pcap_findalldevs_ex() is called to list files and remote adapters.
	 * In the first case, the name of the directory we have to look into must be present (therefore
	 * the 'name' parameter of the pcap_parsesrcstr() is present).
	 * In the second case, the name of the adapter is not required (we need just the host). So, we have
	 * to use a first time this function to get the source type, and a second time to get the appropriate
	 * info, which depends on the source type.
	 */
	if (pcap_parsesrcstr(source, &type, NULL, NULL, NULL, errbuf) == -1)
		return -1;

	switch (type)
	{
	case PCAP_SRC_IFLOCAL:
		if (pcap_parsesrcstr(source, &type, NULL, NULL, NULL, errbuf) == -1)
			return -1;

		/* Initialize temporary string */
		tmpstring[PCAP_BUF_SIZE] = 0;

		/* The user wants to retrieve adapters from a local host */
		if (pcap_findalldevs(alldevs, errbuf) == -1)
			return -1;

		if (*alldevs == NULL)
		{
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
				"No interfaces found! Make sure libpcap/WinPcap is properly installed"
				" on the local machine.");
			return -1;
		}

		/* Scan all the interfaces and modify name and description */
		/* This is a trick in order to avoid the re-implementation of the pcap_findalldevs here */
		dev = *alldevs;
		while (dev)
		{
			/* Create the new device identifier */
			if (pcap_createsrcstr(tmpstring, PCAP_SRC_IFLOCAL, NULL, NULL, dev->name, errbuf) == -1)
				return -1;

			/* Delete the old pointer */
			free(dev->name);

			/* Make a copy of the new device identifier */
			dev->name = strdup(tmpstring);
			if (dev->name == NULL)
			{
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "malloc() failed");
				pcap_freealldevs(*alldevs);
				return -1;
			}

			/* Create the new device description */
			if ((dev->description == NULL) || (dev->description[0] == 0))
				pcap_snprintf(tmpstring, sizeof(tmpstring) - 1, "%s '%s' %s", PCAP_TEXT_SOURCE_ADAPTER,
				dev->name, PCAP_TEXT_SOURCE_ON_LOCAL_HOST);
			else
				pcap_snprintf(tmpstring, sizeof(tmpstring) - 1, "%s '%s' %s", PCAP_TEXT_SOURCE_ADAPTER,
				dev->description, PCAP_TEXT_SOURCE_ON_LOCAL_HOST);

			/* Delete the old pointer */
			free(dev->description);

			/* Make a copy of the description */
			dev->description = strdup(tmpstring);
			if (dev->description == NULL)
			{
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "malloc() failed");
				pcap_freealldevs(*alldevs);
				return -1;
			}

			dev = dev->next;
		}

		return 0;

	case PCAP_SRC_FILE:
	{
		size_t stringlen;
#ifdef _WIN32
		WIN32_FIND_DATA filedata;
		HANDLE filehandle;
#else
		struct dirent *filedata;
		DIR *unixdir;
#endif

		if (pcap_parsesrcstr(source, &type, NULL, NULL, name, errbuf) == -1)
			return -1;

		/* Check that the filename is correct */
		stringlen = strlen(name);

		/* The directory must end with '\' in Win32 and '/' in UNIX */
#ifdef _WIN32
#define ENDING_CHAR '\\'
#else
#define ENDING_CHAR '/'
#endif

		if (name[stringlen - 1] != ENDING_CHAR)
		{
			name[stringlen] = ENDING_CHAR;
			name[stringlen + 1] = 0;

			stringlen++;
		}

		/* Save the path for future reference */
		pcap_snprintf(path, sizeof(path), "%s", name);

#ifdef _WIN32
		/* To perform directory listing, Win32 must have an 'asterisk' as ending char */
		if (name[stringlen - 1] != '*')
		{
			name[stringlen] = '*';
			name[stringlen + 1] = 0;
		}

		filehandle = FindFirstFile(name, &filedata);

		if (filehandle == INVALID_HANDLE_VALUE)
		{
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "Error when listing files: does folder '%s' exist?", path);
			return -1;
		}

#else
		/* opening the folder */
		unixdir= opendir(path);

		/* get the first file into it */
		filedata= readdir(unixdir);

		if (filedata == NULL)
		{
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "Error when listing files: does folder '%s' exist?", path);
			return -1;
		}
#endif

		/* Add all files we find to the list. */
		do
		{

#ifdef _WIN32
			pcap_snprintf(filename, sizeof(filename), "%s%s", path, filedata.cFileName);
#else
			pcap_snprintf(filename, sizeof(filename), "%s%s", path, filedata->d_name);
#endif

			fp = pcap_open_offline(filename, errbuf);

			if (fp)
			{
				/* allocate the main structure */
				dev = (pcap_if_t *)malloc(sizeof(pcap_if_t));
				if (dev == NULL)
				{
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "malloc() failed");
					pcap_freealldevs(*alldevs);
					return -1;
				}

				/* Initialize the structure to 'zero' */
				memset(dev, 0, sizeof(pcap_if_t));

				/* Append it to the list. */
				if (lastdev == NULL)
				{
					/*
					 * List is empty, so it's also
					 * the first device.
					 */
					*alldevs = dev;
				}
				else
				{
					/*
					 * Append after the last device.
					 */
					lastdev->next = dev;
				}
				/* It's now the last device. */
				lastdev = dev;

				/* Create the new source identifier */
				if (pcap_createsrcstr(tmpstring, PCAP_SRC_FILE, NULL, NULL, filename, errbuf) == -1)
				{
					pcap_freealldevs(*alldevs);
					return -1;
				}

				stringlen = strlen(tmpstring);

				dev->name = (char *)malloc(stringlen + 1);
				if (dev->name == NULL)
				{
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "malloc() failed");
					pcap_freealldevs(*alldevs);
					return -1;
				}

				strlcpy(dev->name, tmpstring, stringlen);

				dev->name[stringlen] = 0;

				/* Create the description */
				pcap_snprintf(tmpstring, sizeof(tmpstring) - 1, "%s '%s' %s", PCAP_TEXT_SOURCE_FILE,
					filename, PCAP_TEXT_SOURCE_ON_LOCAL_HOST);

				stringlen = strlen(tmpstring);

				dev->description = (char *)malloc(stringlen + 1);

				if (dev->description == NULL)
				{
					pcap_fmt_errmsg_for_errno(errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "malloc() failed");
					pcap_freealldevs(*alldevs);
					return -1;
				}

				/* Copy the new device description into the correct memory location */
				strlcpy(dev->description, tmpstring, stringlen + 1);

				pcap_close(fp);
			}
		}
#ifdef _WIN32
		while (FindNextFile(filehandle, &filedata) != 0);
#else
		while ( (filedata= readdir(unixdir)) != NULL);
#endif


#ifdef _WIN32
		/* Close the search handle. */
		FindClose(filehandle);
#endif

		return 0;
	}

	case PCAP_SRC_IFREMOTE:
		return pcap_findalldevs_ex_remote(source, auth, alldevs, errbuf);

	default:
		strlcpy(errbuf, "Source type not supported", PCAP_ERRBUF_SIZE);
		return -1;
	}
}

pcap_t *pcap_open(const char *source, int snaplen, int flags, int read_timeout, struct pcap_rmtauth *auth, char *errbuf)
{
	char name[PCAP_BUF_SIZE];
	int type;
	pcap_t *fp;
	int status;

	if (strlen(source) > PCAP_BUF_SIZE)
	{
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "The source string is too long. Cannot handle it correctly.");
		return NULL;
	}

	/*
	 * Determine the type of the source (file, local, remote) and,
	 * if it's file or local, the name of the file or capture device.
	 */
	if (pcap_parsesrcstr(source, &type, NULL, NULL, name, errbuf) == -1)
		return NULL;

	switch (type)
	{
	case PCAP_SRC_FILE:
		return pcap_open_offline(name, errbuf);

	case PCAP_SRC_IFLOCAL:
		fp = pcap_create(name, errbuf);
		break;

	case PCAP_SRC_IFREMOTE:
		/*
		 * Although we already have host, port and iface, we prefer
		 * to pass only 'source' to pcap_open_rpcap(), so that it
		 * has to call pcap_parsesrcstr() again.
		 * This is less optimized, but much clearer.
		 */
		return pcap_open_rpcap(source, snaplen, flags, read_timeout, auth, errbuf);

	default:
		strlcpy(errbuf, "Source type not supported", PCAP_ERRBUF_SIZE);
		return NULL;
	}

	if (fp == NULL)
		return (NULL);
	status = pcap_set_snaplen(fp, snaplen);
	if (status < 0)
		goto fail;
	if (flags & PCAP_OPENFLAG_PROMISCUOUS)
	{
		status = pcap_set_promisc(fp, 1);
		if (status < 0)
			goto fail;
	}
	if (flags & PCAP_OPENFLAG_MAX_RESPONSIVENESS)
	{
		status = pcap_set_immediate_mode(fp, 1);
		if (status < 0)
			goto fail;
	}
#ifdef _WIN32
	/*
	 * This flag is supported on Windows only.
	 * XXX - is there a way to support it with
	 * the capture mechanisms on UN*X?  It's not
	 * exactly a "set direction" operation; I
	 * think it means "do not capture packets
	 * injected with pcap_sendpacket() or
	 * pcap_inject()".
	 */
	/* disable loopback capture if requested */
	if (flags & PCAP_OPENFLAG_NOCAPTURE_LOCAL)
		fp->opt.nocapture_local = 1;
#endif /* _WIN32 */
	status = pcap_set_timeout(fp, read_timeout);
	if (status < 0)
		goto fail;
	status = pcap_activate(fp);
	if (status < 0)
		goto fail;
	return fp;

fail:
	if (status == PCAP_ERROR)
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s",
		    name, fp->errbuf);
	else if (status == PCAP_ERROR_NO_SUCH_DEVICE ||
	    status == PCAP_ERROR_PERM_DENIED ||
	    status == PCAP_ERROR_PROMISC_PERM_DENIED)
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s (%s)",
		    name, pcap_statustostr(status), fp->errbuf);
	else
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "%s: %s",
		    name, pcap_statustostr(status));
	pcap_close(fp);
	return NULL;
}

struct pcap_samp *pcap_setsampling(pcap_t *p)
{
	return &p->rmt_samp;
}

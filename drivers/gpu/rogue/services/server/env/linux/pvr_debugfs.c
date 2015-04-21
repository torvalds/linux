/*************************************************************************/ /*!
@File
@Title          Functions for creating debugfs directories and entries.
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/module.h>
#include <linux/slab.h>

#include "pvr_debug.h"
#include "pvr_debugfs.h"

#define PVR_DEBUGFS_DIR_NAME "pvr"

static struct dentry *gpsPVRDebugFSEntryDir = NULL;


/*************************************************************************/ /*!
 Statistic entry read functions
*/ /**************************************************************************/

typedef struct _PVR_DEBUGFS_DRIVER_STAT_
{
	struct dentry			*psEntry;
	void				*pvData;
	PVRSRV_GET_NEXT_STAT_FUNC	*pfnGetNextStat;
	IMG_INT32			i32StatValue;
	IMG_CHAR			*pszStatFormat;
} PVR_DEBUGFS_DRIVER_STAT;


static void *_DebugFSStatisticSeqStart(struct seq_file *psSeqFile, loff_t *puiPosition)
{
	PVR_DEBUGFS_DRIVER_STAT *psStatData = (PVR_DEBUGFS_DRIVER_STAT *)psSeqFile->private;
	IMG_BOOL bResult;

	bResult = psStatData->pfnGetNextStat(psStatData->pvData,
					    (IMG_UINT32)(*puiPosition),
					    &psStatData->i32StatValue,
					    &psStatData->pszStatFormat);

	return bResult ? psStatData : NULL;
}

static void _DebugFSStatisticSeqStop(struct seq_file *psSeqFile, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(psSeqFile);
	PVR_UNREFERENCED_PARAMETER(pvData);
}

static void *_DebugFSStatisticSeqNext(struct seq_file *psSeqFile,
				      void *pvData,
				      loff_t *puiPosition)
{
	PVR_DEBUGFS_DRIVER_STAT *psStatData = (PVR_DEBUGFS_DRIVER_STAT *)psSeqFile->private;
	IMG_BOOL bResult;

	(*puiPosition)++;

	bResult = psStatData->pfnGetNextStat(psStatData->pvData,
					    (IMG_UINT32)(*puiPosition),
					    &psStatData->i32StatValue,
					    &psStatData->pszStatFormat);

	return bResult ? psStatData : NULL;
}

static int _DebugFSStatisticSeqShow(struct seq_file *psSeqFile, void *pvData)
{
	PVR_DEBUGFS_DRIVER_STAT *psStatData = (PVR_DEBUGFS_DRIVER_STAT *)pvData;

	if (psStatData != NULL)
	{
		if (psStatData->pszStatFormat == NULL)
		{
			return -EINVAL;
		}

		seq_printf(psSeqFile, psStatData->pszStatFormat, psStatData->i32StatValue);
	}

	return 0;
}

static struct seq_operations gsDebugFSStatisticReadOps =
{
	.start = _DebugFSStatisticSeqStart,
	.stop = _DebugFSStatisticSeqStop,
	.next = _DebugFSStatisticSeqNext,
	.show = _DebugFSStatisticSeqShow,
};


/*************************************************************************/ /*!
 Common internal API
*/ /**************************************************************************/

typedef struct _PVR_DEBUGFS_PRIV_DATA_
{
	struct seq_operations	*psReadOps;
	PVRSRV_ENTRY_WRITE_FUNC	*pfnWrite;
	void			*pvData;
} PVR_DEBUGFS_PRIV_DATA;

static int _DebugFSFileOpen(struct inode *psINode, struct file *psFile)
{
	PVR_DEBUGFS_PRIV_DATA *psPrivData = (PVR_DEBUGFS_PRIV_DATA *)psINode->i_private;
	int iResult;

	iResult = seq_open(psFile, psPrivData->psReadOps);
	if (iResult == 0)
	{
		struct seq_file *psSeqFile = psFile->private_data;

		psSeqFile->private = psPrivData->pvData;
	}

	return iResult;
}

static ssize_t _DebugFSFileWrite(struct file *psFile,
				 const char __user *pszBuffer,
				 size_t uiCount,
				 loff_t *puiPosition)
{
	struct inode *psINode = psFile->f_path.dentry->d_inode;
	PVR_DEBUGFS_PRIV_DATA *psPrivData = (PVR_DEBUGFS_PRIV_DATA *)psINode->i_private;

	if (psPrivData->pfnWrite == NULL)
	{
		return -EIO;
	}

	return psPrivData->pfnWrite(pszBuffer, uiCount, *puiPosition, psPrivData->pvData);
}

static const struct file_operations gsPVRDebugFSFileOps =
{
	.owner = THIS_MODULE,
	.open = _DebugFSFileOpen,
	.read = seq_read,
	.write = _DebugFSFileWrite,
	.llseek = seq_lseek,
	.release = seq_release,
};


/*************************************************************************/ /*!
 Public API
*/ /**************************************************************************/

/*************************************************************************/ /*!
@Function       PVRDebugFSInit
@Description    Initialise PVR debugfs support. This should be called before
                using any PVRDebugFS functions.
@Return         int      On success, returns 0. Otherwise, returns an
                         error code.
*/ /**************************************************************************/
int PVRDebugFSInit(void)
{
	PVR_ASSERT(gpsPVRDebugFSEntryDir == NULL);

	gpsPVRDebugFSEntryDir = debugfs_create_dir(PVR_DEBUGFS_DIR_NAME, NULL);
	if (gpsPVRDebugFSEntryDir == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Cannot create '%s' debugfs root directory",
			 __FUNCTION__, PVR_DEBUGFS_DIR_NAME));

		return -ENOMEM;
	}

	return 0;
}

/*************************************************************************/ /*!
@Function       PVRDebugFSDeInit
@Description    Deinitialise PVR debugfs support. This should be called only
                if PVRDebugFSInit() has already been called. All debugfs
                directories and entries should be removed otherwise this
                function will fail.
@Return         void
*/ /**************************************************************************/
void PVRDebugFSDeInit(void)
{
	PVR_ASSERT(gpsPVRDebugFSEntryDir != NULL);

	debugfs_remove(gpsPVRDebugFSEntryDir);
	gpsPVRDebugFSEntryDir = NULL;
}

/*************************************************************************/ /*!
@Function       PVRDebugFSCreateEntryDir
@Description    Create a directory for debugfs entries that will be located
                under the root directory, as created by
                PVRDebugFSCreateEntries().
@Input          pszName      String containing the name for the directory.
@Input          psParentDir  The parent directory in which to create the new
                             directory. This should either be NULL, meaning it
                             should be created in the root directory, or a
                             pointer to a directory as returned by this
                             function.
@Output         ppsDir       On success, points to the newly created
                             directory.
@Return         int          On success, returns 0. Otherwise, returns an
                             error code.
*/ /**************************************************************************/
int PVRDebugFSCreateEntryDir(IMG_CHAR *pszName,
			     struct dentry *psParentDir,
			     struct dentry **ppsDir)
{
	struct dentry *psDir;

	PVR_ASSERT(gpsPVRDebugFSEntryDir != NULL);

	if (pszName == NULL || ppsDir == NULL)
	{
		return -EINVAL;
	}

	psDir = debugfs_create_dir(pszName,
				   (psParentDir) ? psParentDir : gpsPVRDebugFSEntryDir);
	if (psDir == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Cannot create '%s' debugfs directory",
			 __FUNCTION__, pszName));

		return -ENOMEM;
	}

	*ppsDir = psDir;

	return 0;
}

/*************************************************************************/ /*!
@Function       PVRDebugFSRemoveEntryDir
@Description    Remove a directory that was created by
                PVRDebugFSCreateEntryDir(). Any directories or files created
                under the directory being removed should be removed first.
@Input          psDir        Pointer representing the directory to be removed.
@Return         void
*/ /**************************************************************************/
void PVRDebugFSRemoveEntryDir(struct dentry *psDir)
{
	debugfs_remove(psDir);
}

/*************************************************************************/ /*!
@Function       PVRDebugFSCreateEntry
@Description    Create an entry in the specified directory.
@Input          pszName         String containing the name for the entry.
@Input          pvDir           Pointer from PVRDebugFSCreateEntryDir()
                                representing the directory in which to create
                                the entry or NULL for the root directory.
@Input          psReadOps       Pointer to structure containing the necessary
                                functions to read from the entry.
@Input          pfnWrite        Callback function used to write to the entry.
@Input          pvData          Private data to be passed to the read
                                functions, in the seq_file private member, and
                                the write function callback.
@Output         ppsEntry        On success, points to the newly created entry.
@Return         int             On success, returns 0. Otherwise, returns an
                                error code.
*/ /**************************************************************************/
int PVRDebugFSCreateEntry(const char *pszName,
			  void *pvDir,
			  struct seq_operations *psReadOps,
			  PVRSRV_ENTRY_WRITE_FUNC *pfnWrite,
			  void *pvData,
			  struct dentry **ppsEntry)
{
	PVR_DEBUGFS_PRIV_DATA *psPrivData;
	struct dentry *psEntry;
	umode_t uiMode;

	PVR_ASSERT(gpsPVRDebugFSEntryDir != NULL);

	psPrivData = kmalloc(sizeof(*psPrivData), GFP_KERNEL);
	if (psPrivData == NULL)
	{
		return -ENOMEM;
	}

	psPrivData->psReadOps = psReadOps;
	psPrivData->pfnWrite = pfnWrite;
	psPrivData->pvData = pvData;

	uiMode = S_IFREG;

	if (psReadOps != NULL)
	{
		uiMode |= S_IRUGO;
	}

	if (pfnWrite != NULL)
	{
		uiMode |= S_IWUSR;
	}

	psEntry = debugfs_create_file(pszName,
				      uiMode,
				      (pvDir != NULL) ? (struct dentry *)pvDir : gpsPVRDebugFSEntryDir,
				      psPrivData,
				      &gsPVRDebugFSFileOps);
	if (IS_ERR(psEntry))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Cannot create debugfs '%s' file",
			 __FUNCTION__, pszName));

		return PTR_ERR(psEntry);
	}

	/* take reference on inode (for allocation held in d_inode->i_private) - stops
	 * inode being removed until we have freed the memory allocated in i_private */
	igrab(psEntry->d_inode);

	*ppsEntry = psEntry;

	return 0;
}

/*************************************************************************/ /*!
@Function       PVRDebugFSRemoveEntry
@Description    Removes an entry that was created by PVRDebugFSCreateEntry().
@Input          psEntry  Pointer representing the entry to be removed.
@Return         void
*/ /**************************************************************************/
void PVRDebugFSRemoveEntry(struct dentry *psEntry)
{
	if(psEntry != IMG_NULL)
	{
		/* Free any private data that was provided to debugfs_create_file() */
		if (psEntry->d_inode->i_private != NULL)
		{
			kfree(psEntry->d_inode->i_private);
			/* drop reference on inode now that we have freed the allocated memory*/
			iput(psEntry->d_inode);
		}

		debugfs_remove(psEntry);
	}
}

/*************************************************************************/ /*!
@Function       PVRDebugFSCreateStatisticEntry
@Description    Create a statistic entry in the specified directory.
@Input          pszName         String containing the name for the entry.
@Input          pvDir           Pointer from PVRDebugFSCreateEntryDir()
                                representing the directory in which to create
                                the entry or NULL for the root directory.
@Input          pfnGetNextStat  A callback function used to get the next
                                statistic when reading from the statistic
                                entry.
@Input          pvData          Private data to be passed to the provided
                                callback function.
@Return         void *          On success, a pointer representing the newly
                                created statistic entry. Otherwise, NULL.
*/ /**************************************************************************/
void *PVRDebugFSCreateStatisticEntry(const char *pszName,
				     void *pvDir,
				     PVRSRV_GET_NEXT_STAT_FUNC *pfnGetNextStat,
				     void *pvData)
{
	PVR_DEBUGFS_DRIVER_STAT *psStatData;
	int iResult;

	if (pszName == NULL || pfnGetNextStat == NULL)
	{
		return NULL;
	}

	psStatData = kzalloc(sizeof(*psStatData), GFP_KERNEL);
	if (psStatData == NULL)
	{
		return NULL;
	}

	psStatData->pvData = pvData;
	psStatData->pfnGetNextStat = pfnGetNextStat;

	iResult = PVRDebugFSCreateEntry(pszName,
					pvDir,
					&gsDebugFSStatisticReadOps,
					NULL,
					psStatData,
					&psStatData->psEntry);
	if (iResult != 0)
	{
		kfree(psStatData);
		return NULL;
	}

	return psStatData;
}

/*************************************************************************/ /*!
@Function       PVRDebugFSRemoveStatisticEntry
@Description    Removes a statistic entry that was created by
                PVRDebugFSCreateStatisticEntry().
@Input          pvEntry  Pointer representing the statistic entry to be
                         removed.
@Return         void
*/ /**************************************************************************/
void PVRDebugFSRemoveStatisticEntry(void *pvStatEntry)
{
	PVR_DEBUGFS_DRIVER_STAT *psStatData = (PVR_DEBUGFS_DRIVER_STAT *)pvStatEntry;

	if (psStatData != NULL)
	{
		PVRDebugFSRemoveEntry(psStatData->psEntry);

		kfree(psStatData);
	}
}


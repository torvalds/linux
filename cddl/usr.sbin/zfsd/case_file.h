/*-
 * Copyright (c) 2011, 2012, 2013 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 *
 * $FreeBSD$
 */

/**
 * \file case_file.h
 *
 * CaseFile objects aggregate vdev faults that may require ZFSD action
 * in order to maintain the health of a ZFS pool.
 *
 * Header requirements:
 *
 *    #include <list>
 *
 *    #include "callout.h"
 *    #include "zfsd_event.h"
 */
#ifndef _CASE_FILE_H_
#define	_CASE_FILE_H_

/*=========================== Forward Declarations ===========================*/
class CaseFile;
class Vdev;

/*============================= Class Definitions ============================*/
/*------------------------------- CaseFileList -------------------------------*/
/**
 * CaseFileList is a specialization of the standard list STL container.
 */
typedef std::list< CaseFile *> CaseFileList;

/*--------------------------------- CaseFile ---------------------------------*/
/**
 * A CaseFile object is instantiated anytime a vdev for an active pool
 * experiences an I/O error, is faulted by ZFS, or is determined to be
 * missing/removed.
 *
 * A vdev may have at most one CaseFile.
 *
 * CaseFiles are retired when a vdev leaves an active pool configuration
 * or an action is taken to resolve the issues recorded in the CaseFile.
 *
 * Logging a case against a vdev does not imply that an immediate action
 * to resolve a fault is required or even desired.  For example, a CaseFile
 * must accumulate a number of I/O errors in order to flag a device as
 * degraded.
 *
 * Vdev I/O errors are not recorded in ZFS label inforamation.  For this
 * reasons, CaseFile%%s with accumulated I/O error events are serialized
 * to the file system so that they survive across boots.  Currently all
 * other fault types can be reconstructed from ZFS label information, so
 * CaseFile%%s for missing, faulted, or degradded members are just recreated
 * at ZFSD startup instead of being deserialized from the file system.
 */
class CaseFile
{
public:
	/**
	 * \brief Find a CaseFile object by a vdev's pool/vdev GUID tuple.
	 *
	 * \param poolGUID  Pool GUID for the vdev of the CaseFile to find.
	 * 		    If InvalidGuid, then only match the vdev GUID
	 * 		    instead of both pool and vdev GUIDs.
	 * \param vdevGUID  Vdev GUID for the vdev of the CaseFile to find.
	 *
	 * \return  If found, a pointer to a valid CaseFile object.
	 *          Otherwise NULL.
	 */
	static CaseFile *Find(DevdCtl::Guid poolGUID, DevdCtl::Guid vdevGUID);

	/**
	 * \brief Find a CaseFile object by a vdev's current/last known
	 *        physical path.
	 *
	 * \param physPath  Physical path of the vdev of the CaseFile to find.
	 *
	 * \return  If found, a pointer to a valid CaseFile object.
	 *          Otherwise NULL.
	 */
	static CaseFile *Find(const string &physPath);

	/**
	 * \brief ReEvaluate all open cases whose pool guid matches the argument
	 *
	 * \param poolGUID	Only reevaluate cases for this pool
	 * \param event		Try to consume this event with the casefile
	 */
	static void ReEvaluateByGuid(DevdCtl::Guid poolGUID,
				     const ZfsEvent &event);

	/**
	 * \brief Create or return an existing active CaseFile for the
	 *        specified vdev.
	 *
	 * \param vdev  The vdev object for which to find/create a CaseFile.
	 *
	 * \return  A reference to a valid CaseFile object.
	 */
	static CaseFile &Create(Vdev &vdev);

	/**
	 * \brief Deserialize all serialized CaseFile objects found in
	 *        the file system.
	 */
	static void      DeSerialize();

	/**
	 * \brief returns true if there are no CaseFiles
	 */
	static bool	Empty();

	/**
	 * \brief Emit syslog data on all active CaseFile%%s in the system.
	 */
	static void      LogAll();

	/**
	 * \brief Destroy the in-core cache of CaseFile data.
	 *
	 * This routine does not disturb the on disk, serialized, CaseFile
	 * data.
	 */
	static void      PurgeAll();

	DevdCtl::Guid PoolGUID()       const;
	DevdCtl::Guid VdevGUID()       const;
	vdev_state    VdevState()      const;
	const string &PoolGUIDString() const;
	const string &VdevGUIDString() const;
	const string &PhysicalPath()   const;

	/**
	 * \brief Attempt to resolve this CaseFile using the disk
	 *        resource at the given device/physical path/vdev object
	 *        tuple.
	 *
	 * \param devPath   The devfs path for the disk resource.
	 * \param physPath  The physical path information reported by
	 *                  the disk resource.
	 * \param vdev      If the disk contains ZFS label information,
	 *                  a pointer to the disk label's vdev object
	 *                  data.  Otherwise NULL.
	 *
	 * \return  True if this event was consumed by this CaseFile.
	 */
	bool ReEvaluate(const string &devPath, const string &physPath,
			Vdev *vdev);

	/**
	 * \brief Update this CaseFile in light of the provided ZfsEvent.
	 *
	 * Must be virtual so it can be overridden in the unit tests
	 *
	 * \param event  The ZfsEvent to evaluate.
	 *
	 * \return  True if this event was consumed by this CaseFile.
	 */
	virtual bool ReEvaluate(const ZfsEvent &event);

	/**
	 * \brief Register an itimer callout for the given event, if necessary
	 */
	virtual void RegisterCallout(const DevdCtl::Event &event);

	/**
	 * \brief Close a case if it is no longer relevant.
	 *
	 * This method deals with cases tracking soft errors.  Soft errors
	 * will be discarded should a remove event occur within a short period
	 * of the soft errors being reported.  We also discard the events
	 * if the vdev is marked degraded or failed.
	 *
	 * \return  True if the case is closed.  False otherwise.
	 */
	bool CloseIfSolved();

	/**
	 * \brief Emit data about this CaseFile via syslog(3).
	 */
	void Log();

	/**
	 * \brief Whether we should degrade this vdev
	 */
	bool ShouldDegrade() const;

	/**
	 * \brief Whether we should fault this vdev
	 */
	bool ShouldFault() const;

protected:
	enum {
		/**
		 * The number of soft errors on a vdev required
		 * to transition a vdev from healthy to degraded
		 * status.
		 */
		ZFS_DEGRADE_IO_COUNT = 50
	};

	static CalloutFunc_t OnGracePeriodEnded;

	/**
	 * \brief scandir(3) filter function used to find files containing
	 *        serialized CaseFile data.
	 *
	 * \param dirEntry  Directory entry for the file to filter.
	 *
	 * \return  Non-zero for a file to include in the selection,
	 *          otherwise 0.
	 */
	static int  DeSerializeSelector(const struct dirent *dirEntry);

	/**
	 * \brief Given the name of a file containing serialized events from a
	 *        CaseFile object, create/update an in-core CaseFile object
	 *        representing the serialized data.
	 *
	 * \param fileName  The name of a file containing serialized events
	 *                  from a CaseFile object.
	 */
	static void DeSerializeFile(const char *fileName);

	/** Constructor. */
	CaseFile(const Vdev &vdev);

	/**
	 * Destructor.
	 * Must be virtual so it can be subclassed in the unit tests
	 */
	virtual ~CaseFile();

	/**
	 * \brief Reload state for the vdev associated with this CaseFile.
	 *
	 * \return  True if the refresh was successful.  False if the system
	 *          has no record of the pool or vdev for this CaseFile.
	 */
	virtual bool RefreshVdevState();

	/**
	 * \brief Free all events in the m_events list.
	 */
	void PurgeEvents();

	/**
	 * \brief Free all events in the m_tentativeEvents list.
	 */
	void PurgeTentativeEvents();

	/**
	 * \brief Commit to file system storage.
	 */
	void Serialize();

	/**
	 * \brief Retrieve event data from a serialization stream.
	 *
	 * \param caseStream  The serializtion stream to parse.
	 */
	void DeSerialize(std::ifstream &caseStream);

	/**
	 * \brief Serializes the supplied event list and writes it to fd
	 *
	 * \param prefix  If not NULL, this prefix will be prepended to
	 *                every event in the file.
	 */
	void SerializeEvList(const DevdCtl::EventList events, int fd,
			     const char* prefix=NULL) const;

	/**
	 * \brief Unconditionally close a CaseFile.
	 */
	virtual void Close();

	/**
	 * \brief Callout callback invoked when the remove timer grace
	 *        period expires.
	 *
	 * If no remove events are received prior to the grace period
	 * firing, then any tentative events are promoted and counted
	 * against the health of the vdev.
	 */
	void OnGracePeriodEnded();

	/**
	 * \brief Attempt to activate a spare on this case's pool.
	 *
	 * Call this whenever a pool becomes degraded.  It will look for any
	 * spare devices and activate one to replace the casefile's vdev.  It
	 * will _not_ close the casefile; that should only happen when the
	 * missing drive is replaced or the user promotes the spare.
	 *
	 * \return True if a spare was activated
	 */
	bool ActivateSpare();

	/**
	 * \brief replace a pool's vdev with another
	 *
	 * \param vdev_type   The type of the new vdev.  Usually either
	 *                    VDEV_TYPE_DISK or VDEV_TYPE_FILE
	 * \param path        The file system path to the new vdev
	 * \param isspare     Whether the new vdev is a spare
	 *
	 * \return            true iff the replacement was successful
	 */
	bool Replace(const char* vdev_type, const char* path, bool isspare);

	/**
	 * \brief Which vdev, if any, is replacing ours.
	 *
	 * \param zhp		Pool handle state from the caller context
	 *
	 * \return		the vdev that is currently replacing ours,
	 *			or NonexistentVdev if there isn't one.
	 */
	Vdev BeingReplacedBy(zpool_handle_t *zhp);

	/**
	 * \brief All CaseFiles being tracked by ZFSD.
	 */
	static CaseFileList  s_activeCases;

	/**
	 * \brief The file system path to serialized CaseFile data.
	 */
	static const string  s_caseFilePath;

	/**
	 * \brief The time ZFSD waits before promoting a tentative event
	 *        into a permanent event.
	 */
	static const timeval s_removeGracePeriod;

	/**
	 * \brief A list of soft error events counted against the health of
	 *        a vdev.
	 */
	DevdCtl::EventList m_events;

	/**
	 * \brief A list of soft error events waiting for a grace period
	 *        expiration before being counted against the health of
	 *        a vdev.
	 */
	DevdCtl::EventList m_tentativeEvents;

	DevdCtl::Guid	   m_poolGUID;
	DevdCtl::Guid	   m_vdevGUID;
	vdev_state	   m_vdevState;
	string		   m_poolGUIDString;
	string		   m_vdevGUIDString;
	string		   m_vdevPhysPath;

	/**
	 * \brief Callout activated when a grace period
	 */
	Callout		  m_tentativeTimer;

private:
	nvlist_t	*CaseVdev(zpool_handle_t *zhp)	const;
};

inline DevdCtl::Guid
CaseFile::PoolGUID() const
{
	return (m_poolGUID);
}

inline DevdCtl::Guid
CaseFile::VdevGUID() const
{
	return (m_vdevGUID);
}

inline vdev_state
CaseFile::VdevState() const
{
	return (m_vdevState);
}

inline const string &
CaseFile::PoolGUIDString() const
{
	return (m_poolGUIDString);
}

inline const string &
CaseFile::VdevGUIDString() const
{
	return (m_vdevGUIDString);
}

inline const string &
CaseFile::PhysicalPath() const
{
	return (m_vdevPhysPath);
}

#endif /* _CASE_FILE_H_ */

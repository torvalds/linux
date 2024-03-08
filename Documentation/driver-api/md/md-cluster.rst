==========
MD Cluster
==========

The cluster MD is a shared-device RAID for a cluster, it supports
two levels: raid1 and raid10 (limited support).


1. On-disk format
=================

Separate write-intent-bitmaps are used for each cluster analde.
The bitmaps record all writes that may have been started on that analde,
and may analt yet have finished. The on-disk layout is::

  0                    4k                     8k                    12k
  -------------------------------------------------------------------
  | idle                | md super            | bm super [0] + bits |
  | bm bits[0, contd]   | bm super[1] + bits  | bm bits[1, contd]   |
  | bm super[2] + bits  | bm bits [2, contd]  | bm super[3] + bits  |
  | bm bits [3, contd]  |                     |                     |

During "analrmal" functioning we assume the filesystem ensures that only
one analde writes to any given block at a time, so a write request will

 - set the appropriate bit (if analt already set)
 - commit the write to all mirrors
 - schedule the bit to be cleared after a timeout.

Reads are just handled analrmally. It is up to the filesystem to ensure
one analde doesn't read from a location where aanalther analde (or the same
analde) is writing.


2. DLM Locks for management
===========================

There are three groups of locks for managing the device:

2.1 Bitmap lock resource (bm_lockres)
-------------------------------------

 The bm_lockres protects individual analde bitmaps. They are named in
 the form bitmap000 for analde 1, bitmap001 for analde 2 and so on. When a
 analde joins the cluster, it acquires the lock in PW mode and it stays
 so during the lifetime the analde is part of the cluster. The lock
 resource number is based on the slot number returned by the DLM
 subsystem. Since DLM starts analde count from one and bitmap slots
 start from zero, one is subtracted from the DLM slot number to arrive
 at the bitmap slot number.

 The LVB of the bitmap lock for a particular analde records the range
 of sectors that are being re-synced by that analde.  Anal other
 analde may write to those sectors.  This is used when a new analdes
 joins the cluster.

2.2 Message passing locks
-------------------------

 Each analde has to communicate with other analdes when starting or ending
 resync, and for metadata superblock updates.  This communication is
 managed through three locks: "token", "message", and "ack", together
 with the Lock Value Block (LVB) of one of the "message" lock.

2.3 new-device management
-------------------------

 A single lock: "anal-new-dev" is used to coordinate the addition of
 new devices - this must be synchronized across the array.
 Analrmally all analdes hold a concurrent-read lock on this device.

3. Communication
================

 Messages can be broadcast to all analdes, and the sender waits for all
 other analdes to ackanalwledge the message before proceeding.  Only one
 message can be processed at a time.

3.1 Message Types
-----------------

 There are six types of messages which are passed:

3.1.1 METADATA_UPDATED
^^^^^^^^^^^^^^^^^^^^^^

   informs other analdes that the metadata has
   been updated, and the analde must re-read the md superblock. This is
   performed synchroanalusly. It is primarily used to signal device
   failure.

3.1.2 RESYNCING
^^^^^^^^^^^^^^^
   informs other analdes that a resync is initiated or
   ended so that each analde may suspend or resume the region.  Each
   RESYNCING message identifies a range of the devices that the
   sending analde is about to resync. This overrides any previous
   analtification from that analde: only one ranged can be resynced at a
   time per-analde.

3.1.3 NEWDISK
^^^^^^^^^^^^^

   informs other analdes that a device is being added to
   the array. Message contains an identifier for that device.  See
   below for further details.

3.1.4 REMOVE
^^^^^^^^^^^^

   A failed or spare device is being removed from the
   array. The slot-number of the device is included in the message.

 3.1.5 RE_ADD:

   A failed device is being re-activated - the assumption
   is that it has been determined to be working again.

 3.1.6 BITMAP_NEEDS_SYNC:

   If a analde is stopped locally but the bitmap
   isn't clean, then aanalther analde is informed to take the ownership of
   resync.

3.2 Communication mechanism
---------------------------

 The DLM LVB is used to communicate within analdes of the cluster. There
 are three resources used for the purpose:

3.2.1 token
^^^^^^^^^^^
   The resource which protects the entire communication
   system. The analde having the token resource is allowed to
   communicate.

3.2.2 message
^^^^^^^^^^^^^
   The lock resource which carries the data to communicate.

3.2.3 ack
^^^^^^^^^

   The resource, acquiring which means the message has been
   ackanalwledged by all analdes in the cluster. The BAST of the resource
   is used to inform the receiving analde that a analde wants to
   communicate.

The algorithm is:

 1. receive status - all analdes have concurrent-reader lock on "ack"::

	sender                         receiver                 receiver
	"ack":CR                       "ack":CR                 "ack":CR

 2. sender get EX on "token",
    sender get EX on "message"::

	sender                        receiver                 receiver
	"token":EX                    "ack":CR                 "ack":CR
	"message":EX
	"ack":CR

    Sender checks that it still needs to send a message. Messages
    received or other events that happened while waiting for the
    "token" may have made this message inappropriate or redundant.

 3. sender writes LVB

    sender down-convert "message" from EX to CW

    sender try to get EX of "ack"

    ::

      [ wait until all receivers have *processed* the "message" ]

                                       [ triggered by bast of "ack" ]
                                       receiver get CR on "message"
                                       receiver read LVB
                                       receiver processes the message
                                       [ wait finish ]
                                       receiver releases "ack"
                                       receiver tries to get PR on "message"

     sender                         receiver                  receiver
     "token":EX                     "message":CR              "message":CR
     "message":CW
     "ack":EX

 4. triggered by grant of EX on "ack" (indicating all receivers
    have processed message)

    sender down-converts "ack" from EX to CR

    sender releases "message"

    sender releases "token"

    ::

                                 receiver upconvert to PR on "message"
                                 receiver get CR of "ack"
                                 receiver release "message"

     sender                      receiver                   receiver
     "ack":CR                    "ack":CR                   "ack":CR


4. Handling Failures
====================

4.1 Analde Failure
----------------

 When a analde fails, the DLM informs the cluster with the slot
 number. The analde starts a cluster recovery thread. The cluster
 recovery thread:

	- acquires the bitmap<number> lock of the failed analde
	- opens the bitmap
	- reads the bitmap of the failed analde
	- copies the set bitmap to local analde
	- cleans the bitmap of the failed analde
	- releases bitmap<number> lock of the failed analde
	- initiates resync of the bitmap on the current analde
	  md_check_recovery is invoked within recover_bitmaps,
	  then md_check_recovery -> metadata_update_start/finish,
	  it will lock the communication by lock_comm.
	  Which means when one analde is resyncing it blocks all
	  other analdes from writing anywhere on the array.

 The resync process is the regular md resync. However, in a clustered
 environment when a resync is performed, it needs to tell other analdes
 of the areas which are suspended. Before a resync starts, the analde
 send out RESYNCING with the (lo,hi) range of the area which needs to
 be suspended. Each analde maintains a suspend_list, which contains the
 list of ranges which are currently suspended. On receiving RESYNCING,
 the analde adds the range to the suspend_list. Similarly, when the analde
 performing resync finishes, it sends RESYNCING with an empty range to
 other analdes and other analdes remove the corresponding entry from the
 suspend_list.

 A helper function, ->area_resyncing() can be used to check if a
 particular I/O range should be suspended or analt.

4.2 Device Failure
==================

 Device failures are handled and communicated with the metadata update
 routine.  When a analde detects a device failure it does analt allow
 any further writes to that device until the failure has been
 ackanalwledged by all other analdes.

5. Adding a new Device
----------------------

 For adding a new device, it is necessary that all analdes "see" the new
 device to be added. For this, the following algorithm is used:

   1.  Analde 1 issues mdadm --manage /dev/mdX --add /dev/sdYY which issues
       ioctl(ADD_NEW_DISK with disc.state set to MD_DISK_CLUSTER_ADD)
   2.  Analde 1 sends a NEWDISK message with uuid and slot number
   3.  Other analdes issue kobject_uevent_env with uuid and slot number
       (Steps 4,5 could be a udev rule)
   4.  In userspace, the analde searches for the disk, perhaps
       using blkid -t SUB_UUID=""
   5.  Other analdes issue either of the following depending on whether
       the disk was found:
       ioctl(ADD_NEW_DISK with disc.state set to MD_DISK_CANDIDATE and
       disc.number set to slot number)
       ioctl(CLUSTERED_DISK_NACK)
   6.  Other analdes drop lock on "anal-new-devs" (CR) if device is found
   7.  Analde 1 attempts EX lock on "anal-new-dev"
   8.  If analde 1 gets the lock, it sends METADATA_UPDATED after
       unmarking the disk as SpareLocal
   9.  If analt (get "anal-new-dev" lock), it fails the operation and sends
       METADATA_UPDATED.
   10. Other analdes get the information whether a disk is added or analt
       by the following METADATA_UPDATED.

6. Module interface
===================

 There are 17 call-backs which the md core can make to the cluster
 module.  Understanding these can give a good overview of the whole
 process.

6.1 join(analdes) and leave()
---------------------------

 These are called when an array is started with a clustered bitmap,
 and when the array is stopped.  join() ensures the cluster is
 available and initializes the various resources.
 Only the first 'analdes' analdes in the cluster can use the array.

6.2 slot_number()
-----------------

 Reports the slot number advised by the cluster infrastructure.
 Range is from 0 to analdes-1.

6.3 resync_info_update()
------------------------

 This updates the resync range that is stored in the bitmap lock.
 The starting point is updated as the resync progresses.  The
 end point is always the end of the array.
 It does *analt* send a RESYNCING message.

6.4 resync_start(), resync_finish()
-----------------------------------

 These are called when resync/recovery/reshape starts or stops.
 They update the resyncing range in the bitmap lock and also
 send a RESYNCING message.  resync_start reports the whole
 array as resyncing, resync_finish reports analne of it.

 resync_finish() also sends a BITMAP_NEEDS_SYNC message which
 allows some other analde to take over.

6.5 metadata_update_start(), metadata_update_finish(), metadata_update_cancel()
-------------------------------------------------------------------------------

 metadata_update_start is used to get exclusive access to
 the metadata.  If a change is still needed once that access is
 gained, metadata_update_finish() will send a METADATA_UPDATE
 message to all other analdes, otherwise metadata_update_cancel()
 can be used to release the lock.

6.6 area_resyncing()
--------------------

 This combines two elements of functionality.

 Firstly, it will check if any analde is currently resyncing
 anything in a given range of sectors.  If any resync is found,
 then the caller will avoid writing or read-balancing in that
 range.

 Secondly, while analde recovery is happening it reports that
 all areas are resyncing for READ requests.  This avoids races
 between the cluster-filesystem and the cluster-RAID handling
 a analde failure.

6.7 add_new_disk_start(), add_new_disk_finish(), new_disk_ack()
---------------------------------------------------------------

 These are used to manage the new-disk protocol described above.
 When a new device is added, add_new_disk_start() is called before
 it is bound to the array and, if that succeeds, add_new_disk_finish()
 is called the device is fully added.

 When a device is added in ackanalwledgement to a previous
 request, or when the device is declared "unavailable",
 new_disk_ack() is called.

6.8 remove_disk()
-----------------

 This is called when a spare or failed device is removed from
 the array.  It causes a REMOVE message to be send to other analdes.

6.9 gather_bitmaps()
--------------------

 This sends a RE_ADD message to all other analdes and then
 gathers bitmap information from all bitmaps.  This combined
 bitmap is then used to recovery the re-added device.

6.10 lock_all_bitmaps() and unlock_all_bitmaps()
------------------------------------------------

 These are called when change bitmap to analne. If a analde plans
 to clear the cluster raid's bitmap, it need to make sure anal other
 analdes are using the raid which is achieved by lock all bitmap
 locks within the cluster, and also those locks are unlocked
 accordingly.

7. Unsupported features
=======================

There are somethings which are analt supported by cluster MD yet.

- change array_sectors.

/*-
 * Copyright (c) 2012, 2013, 2014 Spectra Logic Corporation
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
 * Authors: Alan Somers         (Spectra Logic Corporation)
 */
#include <sys/cdefs.h>

#include <stdarg.h>
#include <syslog.h>

#include <libnvpair.h>
#include <libzfs.h>

#include <list>
#include <map>
#include <sstream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <devdctl/guid.h>
#include <devdctl/event.h>
#include <devdctl/event_factory.h>
#include <devdctl/exception.h>
#include <devdctl/consumer.h>

#include <zfsd/callout.h>
#include <zfsd/vdev_iterator.h>
#include <zfsd/zfsd_event.h>
#include <zfsd/case_file.h>
#include <zfsd/vdev.h>
#include <zfsd/zfsd.h>
#include <zfsd/zfsd_exception.h>
#include <zfsd/zpool_list.h>

#include "libmocks.h"

__FBSDID("$FreeBSD$");

/*================================== Macros ==================================*/
#define	NUM_ELEMENTS(x) (sizeof(x) / sizeof(*x))

/*============================ Namespace Control =============================*/
using std::string;
using std::stringstream;

using DevdCtl::Event;
using DevdCtl::EventFactory;
using DevdCtl::EventList;
using DevdCtl::Guid;
using DevdCtl::NVPairMap;

/* redefine zpool_handle here because libzfs_impl.h is not includable */
struct zpool_handle
{
        libzfs_handle_t *zpool_hdl;
        zpool_handle_t *zpool_next;
        char zpool_name[ZFS_MAX_DATASET_NAME_LEN];
        int zpool_state;
        size_t zpool_config_size;
        nvlist_t *zpool_config;
        nvlist_t *zpool_old_config;
        nvlist_t *zpool_props;
        diskaddr_t zpool_start_block;
};

class MockZfsEvent : public ZfsEvent
{
public:
	MockZfsEvent(Event::Type, NVPairMap&, const string&);
	virtual ~MockZfsEvent() {}

	static BuildMethod MockZfsEventBuilder;

	MOCK_CONST_METHOD0(ProcessPoolEvent, void());

	static EventFactory::Record s_buildRecords[];
};

EventFactory::Record MockZfsEvent::s_buildRecords[] =
{
        { Event::NOTIFY, "ZFS", &MockZfsEvent::MockZfsEventBuilder }
};

MockZfsEvent::MockZfsEvent(Event::Type type, NVPairMap& map,
			   const string& str)
 : ZfsEvent(type, map, str)
{
}

Event *
MockZfsEvent::MockZfsEventBuilder(Event::Type type,
				  NVPairMap &nvpairs,
			  	  const string &eventString)
{
	return (new MockZfsEvent(type, nvpairs, eventString));
}

/*
 * A dummy Vdev class used for testing other classes
 */
class MockVdev : public Vdev
{
public:
	MockVdev(nvlist_t *vdevConfig);
	virtual ~MockVdev() {}

	MOCK_CONST_METHOD0(GUID, Guid());
	MOCK_CONST_METHOD0(PoolGUID, Guid());
	MOCK_CONST_METHOD0(State, vdev_state());
	MOCK_CONST_METHOD0(PhysicalPath, string());
};

MockVdev::MockVdev(nvlist_t *vdevConfig)
 : Vdev(vdevConfig)
{
}

/*
 * A CaseFile class with side effects removed, for testing
 */
class TestableCaseFile : public CaseFile
{
public:
	static TestableCaseFile &Create(Vdev &vdev);
	TestableCaseFile(Vdev &vdev);
	virtual ~TestableCaseFile() {}

	MOCK_METHOD0(Close, void());
	MOCK_METHOD1(RegisterCallout, void(const Event &event));
	MOCK_METHOD0(RefreshVdevState, bool());
	MOCK_METHOD1(ReEvaluate, bool(const ZfsEvent &event));

	bool RealReEvaluate(const ZfsEvent &event)
	{
		return (CaseFile::ReEvaluate(event));
	}

	/*
	 * This splices the event lists, a procedure that would normally be done
	 * by OnGracePeriodEnded, but we don't necessarily call that in the
	 * unit tests
	 */
	void SpliceEvents();

	/*
	 * Used by some of our expectations.  CaseFile does not publicize this
	 */
	static int getActiveCases()
	{
		return (s_activeCases.size());
	}
};

TestableCaseFile::TestableCaseFile(Vdev &vdev)
 : CaseFile(vdev)
{
}

TestableCaseFile &
TestableCaseFile::Create(Vdev &vdev)
{
	TestableCaseFile *newCase;
	newCase = new TestableCaseFile(vdev);
	return (*newCase);
}

void
TestableCaseFile::SpliceEvents()
{
	m_events.splice(m_events.begin(), m_tentativeEvents);
}


/*
 * Test class ZfsdException
 */
class ZfsdExceptionTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		ASSERT_EQ(0, nvlist_alloc(&poolConfig, NV_UNIQUE_NAME, 0));
		ASSERT_EQ(0, nvlist_add_string(poolConfig,
				ZPOOL_CONFIG_POOL_NAME, "unit_test_pool"));
		ASSERT_EQ(0, nvlist_add_uint64(poolConfig,
				ZPOOL_CONFIG_POOL_GUID, 0x1234));

		ASSERT_EQ(0, nvlist_alloc(&vdevConfig, NV_UNIQUE_NAME, 0));
		ASSERT_EQ(0, nvlist_add_uint64(vdevConfig,
				ZPOOL_CONFIG_GUID, 0x5678));
		bzero(&poolHandle, sizeof(poolHandle));
		poolHandle.zpool_config = poolConfig;
	}

	virtual void TearDown()
	{
		nvlist_free(poolConfig);
		nvlist_free(vdevConfig);
	}

	nvlist_t	*poolConfig;
	nvlist_t	*vdevConfig;
	zpool_handle_t   poolHandle;
};

TEST_F(ZfsdExceptionTest, StringConstructorNull)
{
	ZfsdException ze("");
	EXPECT_STREQ("", ze.GetString().c_str());
}

TEST_F(ZfsdExceptionTest, StringConstructorFormatted)
{
	ZfsdException ze(" %d %s", 55, "hello world");
	EXPECT_STREQ(" 55 hello world", ze.GetString().c_str());
}

TEST_F(ZfsdExceptionTest, LogSimple)
{
	ZfsdException ze("unit test w/o vdev or pool");
	ze.Log();
	EXPECT_EQ(LOG_ERR, syslog_last_priority);
	EXPECT_STREQ("unit test w/o vdev or pool\n", syslog_last_message);
}

TEST_F(ZfsdExceptionTest, Pool)
{
	const char msg[] = "Exception with pool name";
	char expected[4096];
	sprintf(expected, "Pool unit_test_pool: %s\n", msg);
	ZfsdException ze(poolConfig, msg);
	ze.Log();
	EXPECT_STREQ(expected, syslog_last_message);
}

TEST_F(ZfsdExceptionTest, PoolHandle)
{
	const char msg[] = "Exception with pool handle";
	char expected[4096];
	sprintf(expected, "Pool unit_test_pool: %s\n", msg);
	ZfsdException ze(&poolHandle, msg);
	ze.Log();
	EXPECT_STREQ(expected, syslog_last_message);
}

/*
 * Test class Vdev
 */
class VdevTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		ASSERT_EQ(0, nvlist_alloc(&m_poolConfig, NV_UNIQUE_NAME, 0));
		ASSERT_EQ(0, nvlist_add_uint64(m_poolConfig,
					       ZPOOL_CONFIG_POOL_GUID,
					       0x1234));

		ASSERT_EQ(0, nvlist_alloc(&m_vdevConfig, NV_UNIQUE_NAME, 0));
		ASSERT_EQ(0, nvlist_add_uint64(m_vdevConfig, ZPOOL_CONFIG_GUID,
					       0x5678));
	}

	virtual void TearDown()
	{
		nvlist_free(m_poolConfig);
		nvlist_free(m_vdevConfig);
	}

	nvlist_t	*m_poolConfig;
	nvlist_t	*m_vdevConfig;
};


TEST_F(VdevTest, StateFromConfig)
{
	vdev_stat_t vs;

	vs.vs_state = VDEV_STATE_OFFLINE;

	ASSERT_EQ(0, nvlist_add_uint64_array(m_vdevConfig,
					     ZPOOL_CONFIG_VDEV_STATS,
					     (uint64_t*)&vs,
					     sizeof(vs) / sizeof(uint64_t)));

	Vdev vdev(m_poolConfig, m_vdevConfig);

	EXPECT_EQ(VDEV_STATE_OFFLINE, vdev.State());
}

TEST_F(VdevTest, StateFaulted)
{
	ASSERT_EQ(0, nvlist_add_uint64(m_vdevConfig, ZPOOL_CONFIG_FAULTED, 1));

	Vdev vdev(m_poolConfig, m_vdevConfig);

	EXPECT_EQ(VDEV_STATE_FAULTED, vdev.State());
}

/*
 * Test that we can construct a Vdev from the label information that is stored
 * on an available spare drive
 */
TEST_F(VdevTest, ConstructAvailSpare)
{
	nvlist_t	*labelConfig;

	ASSERT_EQ(0, nvlist_alloc(&labelConfig, NV_UNIQUE_NAME, 0));
	ASSERT_EQ(0, nvlist_add_uint64(labelConfig, ZPOOL_CONFIG_GUID,
				       1948339428197961030));
	ASSERT_EQ(0, nvlist_add_uint64(labelConfig, ZPOOL_CONFIG_POOL_STATE,
				       POOL_STATE_SPARE));

	EXPECT_NO_THROW(Vdev vdev(labelConfig));

	nvlist_free(labelConfig);
}

/* Available spares will always show the HEALTHY state */
TEST_F(VdevTest, AvailSpareState) {
	nvlist_t	*labelConfig;

	ASSERT_EQ(0, nvlist_alloc(&labelConfig, NV_UNIQUE_NAME, 0));
	ASSERT_EQ(0, nvlist_add_uint64(labelConfig, ZPOOL_CONFIG_GUID,
				       1948339428197961030));
	ASSERT_EQ(0, nvlist_add_uint64(labelConfig, ZPOOL_CONFIG_POOL_STATE,
				       POOL_STATE_SPARE));

	Vdev vdev(labelConfig);
	EXPECT_EQ(VDEV_STATE_HEALTHY, vdev.State());

	nvlist_free(labelConfig);
}

/* Test the Vdev::IsSpare method */
TEST_F(VdevTest, IsSpare) {
	Vdev notSpare(m_poolConfig, m_vdevConfig);
	EXPECT_EQ(false, notSpare.IsSpare());

	ASSERT_EQ(0, nvlist_add_uint64(m_vdevConfig, ZPOOL_CONFIG_IS_SPARE, 1));
	Vdev isSpare(m_poolConfig, m_vdevConfig);
	EXPECT_EQ(true, isSpare.IsSpare());
}

/*
 * Test class ZFSEvent
 */
class ZfsEventTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		m_eventFactory = new EventFactory();
		m_eventFactory->UpdateRegistry(MockZfsEvent::s_buildRecords,
		    NUM_ELEMENTS(MockZfsEvent::s_buildRecords));

		m_event = NULL;
	}

	virtual void TearDown()
	{
		delete m_eventFactory;
		delete m_event;
	}

	EventFactory	*m_eventFactory;
	Event		*m_event;
};

TEST_F(ZfsEventTest, ProcessPoolEventGetsCalled)
{
	string evString("!system=ZFS "
			"subsystem=ZFS "
			"type=misc.fs.zfs.vdev_remove "
			"pool_name=foo "
			"pool_guid=9756779504028057996 "
			"vdev_guid=1631193447431603339 "
			"vdev_path=/dev/da1 "
			"timestamp=1348871594");
	m_event = Event::CreateEvent(*m_eventFactory, evString);
	MockZfsEvent *mock_event = static_cast<MockZfsEvent*>(m_event);

	EXPECT_CALL(*mock_event, ProcessPoolEvent()).Times(1);
	mock_event->Process();
}

/*
 * Test class CaseFile
 */

class CaseFileTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		m_eventFactory = new EventFactory();
		m_eventFactory->UpdateRegistry(MockZfsEvent::s_buildRecords,
		    NUM_ELEMENTS(MockZfsEvent::s_buildRecords));

		m_event = NULL;

		nvlist_alloc(&m_vdevConfig, NV_UNIQUE_NAME, 0);
		ASSERT_EQ(0, nvlist_add_uint64(m_vdevConfig,
					       ZPOOL_CONFIG_GUID, 0xbeef));
		m_vdev = new MockVdev(m_vdevConfig);
		ON_CALL(*m_vdev, GUID())
		    .WillByDefault(::testing::Return(Guid(123)));
		ON_CALL(*m_vdev, PoolGUID())
		    .WillByDefault(::testing::Return(Guid(456)));
		ON_CALL(*m_vdev, State())
		    .WillByDefault(::testing::Return(VDEV_STATE_HEALTHY));
		m_caseFile = &TestableCaseFile::Create(*m_vdev);
		ON_CALL(*m_caseFile, ReEvaluate(::testing::_))
		    .WillByDefault(::testing::Invoke(m_caseFile, &TestableCaseFile::RealReEvaluate));
		return;
	}

	virtual void TearDown()
	{
		delete m_caseFile;
		nvlist_free(m_vdevConfig);
		delete m_vdev;
		delete m_event;
		delete m_eventFactory;
	}

	nvlist_t		*m_vdevConfig;
	MockVdev		*m_vdev;
	TestableCaseFile 	*m_caseFile;
	Event			*m_event;
	EventFactory		*m_eventFactory;
};

/*
 * A Vdev with no events should not be degraded or faulted
 */
TEST_F(CaseFileTest, HealthyVdev)
{
	EXPECT_FALSE(m_caseFile->ShouldDegrade());
	EXPECT_FALSE(m_caseFile->ShouldFault());
}

/*
 * A Vdev with only one event should not be degraded or faulted
 * For performance reasons, RefreshVdevState should not be called.
 */
TEST_F(CaseFileTest, HealthyishVdev)
{
	string evString("!system=ZFS "
			"class=ereport.fs.zfs.io "
			"ena=12091638756982918145 "
			"parent_guid=13237004955564865395 "
			"parent_type=raidz "
			"pool=testpool.4415 "
			"pool_context=0 "
			"pool_failmode=wait "
			"pool_guid=456 "
			"subsystem=ZFS "
			"timestamp=1348867914 "
			"type=ereport.fs.zfs.io "
			"vdev_guid=123 "
			"vdev_path=/dev/da400 "
			"vdev_type=disk "
			"zio_blkid=622 "
			"zio_err=1 "
			"zio_level=-2 "
			"zio_object=0 "
			"zio_objset=37 "
			"zio_offset=25598976 "
			"zio_size=1024");
	m_event = Event::CreateEvent(*m_eventFactory, evString);
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);

	EXPECT_CALL(*m_caseFile, RefreshVdevState())
	    .Times(::testing::Exactly(0));
	EXPECT_TRUE(m_caseFile->ReEvaluate(*zfs_event));
	EXPECT_FALSE(m_caseFile->ShouldDegrade());
	EXPECT_FALSE(m_caseFile->ShouldFault());
}

/* The case file should be closed when its pool is destroyed */
TEST_F(CaseFileTest, PoolDestroy)
{
	string evString("!system=ZFS "
			"pool_name=testpool.4415 "
			"pool_guid=456 "
			"subsystem=ZFS "
			"timestamp=1348867914 "
			"type=misc.fs.zfs.pool_destroy ");
	m_event = Event::CreateEvent(*m_eventFactory, evString);
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);
	EXPECT_CALL(*m_caseFile, Close());
	EXPECT_TRUE(m_caseFile->ReEvaluate(*zfs_event));
}

/*
 * A Vdev with a very large number of IO errors should fault
 * For performance reasons, RefreshVdevState should be called at most once
 */
TEST_F(CaseFileTest, VeryManyIOErrors)
{
	EXPECT_CALL(*m_caseFile, RefreshVdevState())
	    .Times(::testing::AtMost(1))
	    .WillRepeatedly(::testing::Return(true));

	for(int i=0; i<100; i++) {
		stringstream evStringStream;
		evStringStream <<
			"!system=ZFS "
			"class=ereport.fs.zfs.io "
			"ena=12091638756982918145 "
			"parent_guid=13237004955564865395 "
			"parent_type=raidz "
			"pool=testpool.4415 "
			"pool_context=0 "
			"pool_failmode=wait "
			"pool_guid=456 "
			"subsystem=ZFS "
			"timestamp=";
		evStringStream << i << " ";
		evStringStream <<
			"type=ereport.fs.zfs.io "
			"vdev_guid=123 "
			"vdev_path=/dev/da400 "
			"vdev_type=disk "
			"zio_blkid=622 "
			"zio_err=1 "
			"zio_level=-2 "
			"zio_object=0 "
			"zio_objset=37 "
			"zio_offset=25598976 "
			"zio_size=1024";
		Event *event(Event::CreateEvent(*m_eventFactory,
						evStringStream.str()));
		ZfsEvent *zfs_event = static_cast<ZfsEvent*>(event);
		EXPECT_TRUE(m_caseFile->ReEvaluate(*zfs_event));
		delete event;
	}

	m_caseFile->SpliceEvents();
	EXPECT_FALSE(m_caseFile->ShouldDegrade());
	EXPECT_TRUE(m_caseFile->ShouldFault());
}

/*
 * A Vdev with a very large number of checksum errors should degrade
 * For performance reasons, RefreshVdevState should be called at most once
 */
TEST_F(CaseFileTest, VeryManyChecksumErrors)
{
	EXPECT_CALL(*m_caseFile, RefreshVdevState())
	    .Times(::testing::AtMost(1))
	    .WillRepeatedly(::testing::Return(true));

	for(int i=0; i<100; i++) {
		stringstream evStringStream;
		evStringStream <<
			"!system=ZFS "
			"bad_cleared_bits=03000000000000803f50b00000000000 "
			"bad_range_clears=0000000e "
			"bad_range_sets=00000000 "
			"bad_ranges=0000000000000010 "
			"bad_ranges_min_gap=8 "
			"bad_set_bits=00000000000000000000000000000000 "
			"class=ereport.fs.zfs.checksum "
			"ena=12272856582652437505 "
			"parent_guid=5838204195352909894 "
			"parent_type=raidz pool=testpool.7640 "
			"pool_context=0 "
			"pool_failmode=wait "
			"pool_guid=456 "
			"subsystem=ZFS timestamp=";
		evStringStream << i << " ";
		evStringStream <<
			"type=ereport.fs.zfs.checksum "
			"vdev_guid=123 "
			"vdev_path=/mnt/tmp/file1.7702 "
			"vdev_type=file "
			"zio_blkid=0 "
			"zio_err=0 "
			"zio_level=0 "
			"zio_object=3 "
			"zio_objset=0 "
			"zio_offset=16896 "
			"zio_size=512";
		Event *event(Event::CreateEvent(*m_eventFactory,
						evStringStream.str()));
		ZfsEvent *zfs_event = static_cast<ZfsEvent*>(event);
		EXPECT_TRUE(m_caseFile->ReEvaluate(*zfs_event));
		delete event;
	}

	m_caseFile->SpliceEvents();
	EXPECT_TRUE(m_caseFile->ShouldDegrade());
	EXPECT_FALSE(m_caseFile->ShouldFault());
}

/*
 * Test CaseFile::ReEvaluateByGuid
 */
class ReEvaluateByGuidTest : public ::testing::Test
{
protected:
	virtual void SetUp()
	{
		m_eventFactory = new EventFactory();
		m_eventFactory->UpdateRegistry(MockZfsEvent::s_buildRecords,
		    NUM_ELEMENTS(MockZfsEvent::s_buildRecords));
		m_event = Event::CreateEvent(*m_eventFactory, s_evString);
		nvlist_alloc(&m_vdevConfig, NV_UNIQUE_NAME, 0);
		ASSERT_EQ(0, nvlist_add_uint64(m_vdevConfig,
					       ZPOOL_CONFIG_GUID, 0xbeef));
		m_vdev456 = new ::testing::NiceMock<MockVdev>(m_vdevConfig);
		m_vdev789 = new ::testing::NiceMock<MockVdev>(m_vdevConfig);
		ON_CALL(*m_vdev456, GUID())
		    .WillByDefault(::testing::Return(Guid(123)));
		ON_CALL(*m_vdev456, PoolGUID())
		    .WillByDefault(::testing::Return(Guid(456)));
		ON_CALL(*m_vdev456, State())
		    .WillByDefault(::testing::Return(VDEV_STATE_HEALTHY));
		ON_CALL(*m_vdev789, GUID())
		    .WillByDefault(::testing::Return(Guid(123)));
		ON_CALL(*m_vdev789, PoolGUID())
		    .WillByDefault(::testing::Return(Guid(789)));
		ON_CALL(*m_vdev789, State())
		    .WillByDefault(::testing::Return(VDEV_STATE_HEALTHY));
		m_caseFile456 = NULL;
		m_caseFile789 = NULL;
		return;
	}

	virtual void TearDown()
	{
		delete m_caseFile456;
		delete m_caseFile789;
		nvlist_free(m_vdevConfig);
		delete m_vdev456;
		delete m_vdev789;
		delete m_event;
		delete m_eventFactory;
	}

	static string			 s_evString;
	nvlist_t			*m_vdevConfig;
	::testing::NiceMock<MockVdev>	*m_vdev456;
	::testing::NiceMock<MockVdev>	*m_vdev789;
	TestableCaseFile 		*m_caseFile456;
	TestableCaseFile 		*m_caseFile789;
	Event				*m_event;
	EventFactory			*m_eventFactory;
};

string ReEvaluateByGuidTest::s_evString(
	"!system=ZFS "
	"pool_guid=16271873792808333580 "
	"pool_name=foo "
	"subsystem=ZFS "
	"timestamp=1360620391 "
	"type=misc.fs.zfs.config_sync");


/*
 * Test the ReEvaluateByGuid method on an empty list of casefiles.
 * We must create one event, even though it never gets used, because it will
 * be passed by reference to ReEvaluateByGuid
 */
TEST_F(ReEvaluateByGuidTest, ReEvaluateByGuid_empty)
{
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);

	EXPECT_EQ(0, TestableCaseFile::getActiveCases());
	CaseFile::ReEvaluateByGuid(Guid(456), *zfs_event);
	EXPECT_EQ(0, TestableCaseFile::getActiveCases());
}

/*
 * Test the ReEvaluateByGuid method on a list of CaseFiles that contains only
 * one CaseFile, which doesn't match the criteria
 */
TEST_F(ReEvaluateByGuidTest, ReEvaluateByGuid_oneFalse)
{
	m_caseFile456 = &TestableCaseFile::Create(*m_vdev456);
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);

	EXPECT_EQ(1, TestableCaseFile::getActiveCases());
	EXPECT_CALL(*m_caseFile456, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(0));
	CaseFile::ReEvaluateByGuid(Guid(789), *zfs_event);
	EXPECT_EQ(1, TestableCaseFile::getActiveCases());
}

/*
 * Test the ReEvaluateByGuid method on a list of CaseFiles that contains only
 * one CaseFile, which does match the criteria
 */
TEST_F(ReEvaluateByGuidTest, ReEvaluateByGuid_oneTrue)
{
	m_caseFile456 = &TestableCaseFile::Create(*m_vdev456);
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);

	EXPECT_EQ(1, TestableCaseFile::getActiveCases());
	EXPECT_CALL(*m_caseFile456, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(1))
	    .WillRepeatedly(::testing::Return(false));
	CaseFile::ReEvaluateByGuid(Guid(456), *zfs_event);
	EXPECT_EQ(1, TestableCaseFile::getActiveCases());
}

/*
 * Test the ReEvaluateByGuid method on a long list of CaseFiles that contains a
 * few cases which meet the criteria
 */
TEST_F(ReEvaluateByGuidTest, ReEvaluateByGuid_five)
{
	TestableCaseFile *CaseFile1 = &TestableCaseFile::Create(*m_vdev456);
	TestableCaseFile *CaseFile2 = &TestableCaseFile::Create(*m_vdev789);
	TestableCaseFile *CaseFile3 = &TestableCaseFile::Create(*m_vdev456);
	TestableCaseFile *CaseFile4 = &TestableCaseFile::Create(*m_vdev789);
	TestableCaseFile *CaseFile5 = &TestableCaseFile::Create(*m_vdev789);
	ZfsEvent *zfs_event = static_cast<ZfsEvent*>(m_event);

	EXPECT_EQ(5, TestableCaseFile::getActiveCases());
	EXPECT_CALL(*CaseFile1, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(1))
	    .WillRepeatedly(::testing::Return(false));
	EXPECT_CALL(*CaseFile3, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(1))
	    .WillRepeatedly(::testing::Return(false));
	EXPECT_CALL(*CaseFile2, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(0));
	EXPECT_CALL(*CaseFile4, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(0));
	EXPECT_CALL(*CaseFile5, ReEvaluate(::testing::_))
	    .Times(::testing::Exactly(0));
	CaseFile::ReEvaluateByGuid(Guid(456), *zfs_event);
	EXPECT_EQ(5, TestableCaseFile::getActiveCases());
	delete CaseFile1;
	delete CaseFile2;
	delete CaseFile3;
	delete CaseFile4;
	delete CaseFile5;
}

#
# Makefile for the 'event manager' sub-component of powerplay.
# It provides the event management services for the driver.

EVENT_MGR = eventmgr.o eventinit.o eventmanagement.o  \
		eventactionchains.o eventsubchains.o eventtasks.o psm.o

AMD_PP_EVENT = $(addprefix $(AMD_PP_PATH)/eventmgr/,$(EVENT_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_EVENT)


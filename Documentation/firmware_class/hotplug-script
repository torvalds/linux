#!/bin/sh

# Simple hotplug script sample:
# 
# Both $DEVPATH and $FIRMWARE are already provided in the environment.

HOTPLUG_FW_DIR=/usr/lib/hotplug/firmware/

if [ "$SUBSYSTEM" == "firmware" -a "$ACTION" == "add" ]; then
  if [ -f $HOTPLUG_FW_DIR/$FIRMWARE ]; then
    echo 1 > /sys/$DEVPATH/loading
    cat $HOTPLUG_FW_DIR/$FIRMWARE > /sys/$DEVPATH/data
    echo 0 > /sys/$DEVPATH/loading
  else
    echo -1 > /sys/$DEVPATH/loading
  fi
fi

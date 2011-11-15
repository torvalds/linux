adb devices
adb shell mount -o remount,rw /dev/block/nandc /system
adb shell mount -o remount,rw /dev/root /
::adb shell rm initlogo.rle
adb push disp/disp.ko /drv/disp.ko
adb shell chmod 777 /drv/disp.ko
adb shell sync
adb push lcd/lcd.ko /drv/lcd.ko
adb shell chmod 777 /drv/lcd.ko
adb shell sync
adb push hdmi/hdmi.ko /drv/hdmi.ko
adb shell chmod 777 /drv/hdmi.ko
adb shell sync
pause
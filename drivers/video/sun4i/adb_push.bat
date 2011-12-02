adb devices
adb shell mount -o remount,rw /dev/block/nandc /system
adb shell mount -o remount,rw /dev/root /
adb push disp/disp.ko /drv/disp.ko
adb shell chmod 777 /drv/disp.ko
adb push lcd/lcd.ko /drv/lcd.ko
adb shell chmod 777 /drv/lcd.ko
adb push hdmi/hdmi.ko /drv/hdmi.ko
adb shell chmod 777 /drv/hdmi.ko
adb shell sync
echo press any key to reboot
pause
adb shell reboot
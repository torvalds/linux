

rm -f *.o
rm -f ../wifi_power/*.o
rm -f *.uu
rm -rf bcm4319
rm -f .*.cmd
rm -f ../wifi_power/.*.cmd
rm -f modules.order Module.symvers

find . -name '*.o' -exec rm -f {} \;
find . -name '.*.cmd' -exec rm -f {} \;


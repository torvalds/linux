#/bin/bash
#load/unload driver

count=0
S=1
while [ "$S" == "1" ]
do
    ./unload.sh
    ./load.sh
    sleep 10
done


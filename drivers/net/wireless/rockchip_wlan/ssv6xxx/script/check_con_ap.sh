#/bin/bash
#load/unload driver

count=0

S=1
while [ "$S" == "1" ]
do
	  count=$[ count + 1 ]
    sleep 3
    dmesg -c
    ./unload.sh
    ./load.sh
    sleep 3
    ./cli mib reset
    ./cli mib ampdurx
    ./cli hci rxq show
    sleep 60    
    gotIp=$(ifconfig | grep "inet addr:192.168.2.22")
    if [ "$gotIp" == "" ]; then
  		echo -n "[$count]timeout..."
  		addr=$(dmesg | grep "30 49 3b 01 3f c0")
    	if [ "$addr" == "" ]; then	
  			echo "no rx beacon..."
		    ./cli mib ampdurx
		    ./cli hci rxq show
				./unload.sh
  			break
  		else
  			echo "got rx beacon..."
  			./cli mib ampdurx
  			./cli hci rxq show
  			sleep 2
  		fi
		else
		
			addr=$(dmesg | grep "30 49 3b 01 3f c0")
    	if [ "$addr" == "" ]; then	
  			echo "[$count] connet to ap... no rx beacon..."
  		       ./cli mib ampdurx
  		       ./cli hci rxq show
             ./unload.sh
                        
  			break
  		else
  			echo "[$count]connet to ap... got rx beacon..." 
  			./cli mib ampdurx
  			./cli hci rxq show
  			sleep 2 		
  		fi
			 
    fi
    
done


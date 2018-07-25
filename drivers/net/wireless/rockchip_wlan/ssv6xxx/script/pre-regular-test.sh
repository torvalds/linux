#!/bin/bash

echo "=================================================="
echo "For regular test, create files for Disable AMPDU"
echo "Output files on driver root directory:"
echo " load1.sh"
echo " ssvcfg1.sh"
echo " sta1.sh"
echo "=================================================="
cp ../load.sh ../load1.sh
cp ../ssvcfg.sh ../ssvcfg1.sh
cp ../sta.cfg ../sta1.cfg

find ../load1.sh | xargs -i sed -i 's/ssvcfg.sh/ssvcfg1.sh/g' {}
find ../ssvcfg1.sh | xargs -i sed -i 's/sta.cfg/sta1.cfg/g' {}
find ../sta1.cfg | xargs -i sed -i 's/hw_cap_ampdu_rx = on/hw_cap_ampdu_rx = off/g' {}
find ../sta1.cfg | xargs -i sed -i 's/hw_cap_ampdu_tx = on/hw_cap_ampdu_tx = off/g' {}
/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WAPI_SMS4_H
#define WAPI_SMS4_H 
void WapiCryptoSms4(u8 *iv, u8 *key, u8 *input, u16 length, u8 *output);
void WapiCryptoSms4Mic(u8 *iv, u8 *Key, u8 *header, u16 headerLength,
                             const u8 *input, u16 dataLength, u8 *output);
#endif

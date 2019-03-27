{-# LANGUAGE ForeignFunctionInterface #-}

-- an example UCL FFI module:
-- uses the Object Model from Messagepack to emit 
-- 

module Data.UCL ( unpack ) where
import Foreign.C
import Foreign.Ptr
import System.IO.Unsafe ( unsafePerformIO )
import qualified Data.Text as T
import qualified Data.Vector as V
import qualified Data.MessagePack as MSG

type ParserHandle = Ptr ()
type UCLObjectHandle = Ptr ()
type UCLIterHandle = Ptr ()
type UCLEmitterType = CInt
type ErrorString = String


foreign import ccall "ucl_parser_new" ucl_parser_new :: CInt -> ParserHandle
foreign import ccall "ucl_parser_add_string" ucl_parser_add_string :: ParserHandle -> CString -> CUInt -> IO Bool
foreign import ccall "ucl_parser_add_file" ucl_parser_add_file :: ParserHandle -> CString -> IO Bool
foreign import ccall "ucl_parser_get_object" ucl_parser_get_object :: ParserHandle -> UCLObjectHandle
foreign import ccall "ucl_parser_get_error" ucl_parser_get_error :: ParserHandle -> CString

foreign import ccall "ucl_object_iterate_new" ucl_object_iterate_new :: UCLObjectHandle -> UCLIterHandle
foreign import ccall "ucl_object_iterate_safe" ucl_object_iterate_safe :: UCLIterHandle -> Bool -> UCLObjectHandle
foreign import ccall "ucl_object_type" ucl_object_type :: UCLObjectHandle -> CUInt
foreign import ccall "ucl_object_key" ucl_object_key :: UCLObjectHandle -> CString
foreign import ccall "ucl_object_toint" ucl_object_toint :: UCLObjectHandle -> CInt
foreign import ccall "ucl_object_todouble" ucl_object_todouble :: UCLObjectHandle -> CDouble
foreign import ccall "ucl_object_tostring" ucl_object_tostring :: UCLObjectHandle -> CString
foreign import ccall "ucl_object_toboolean" ucl_object_toboolean :: UCLObjectHandle -> Bool

foreign import ccall "ucl_object_emit" ucl_object_emit :: UCLObjectHandle -> UCLEmitterType -> CString
foreign import ccall "ucl_object_emit_len" ucl_object_emit_len :: UCLObjectHandle -> UCLEmitterType -> Ptr CSize -> IO CString

type UCL_TYPE = CUInt
ucl_OBJECT :: UCL_TYPE
ucl_OBJECT = 0
ucl_ARRAY :: UCL_TYPE
ucl_ARRAY = 1
ucl_INT :: UCL_TYPE
ucl_INT = 2
ucl_FLOAT :: UCL_TYPE
ucl_FLOAT = 3
ucl_STRING :: UCL_TYPE
ucl_STRING = 4
ucl_BOOLEAN :: UCL_TYPE
ucl_BOOLEAN = 5
ucl_TIME :: UCL_TYPE
ucl_TIME = 6
ucl_USERDATA :: UCL_TYPE
ucl_USERDATA = 7
ucl_NULL :: UCL_TYPE
ucl_NULL = 8

ucl_emit_json           :: UCLEmitterType
ucl_emit_json         = 0 
ucl_emit_json_compact   :: UCLEmitterType
ucl_emit_json_compact = 1 :: UCLEmitterType
ucl_emit_msgpack        :: UCLEmitterType
ucl_emit_msgpack      = 4 :: UCLEmitterType

ucl_parser_parse_string_pure :: String -> Either UCLObjectHandle ErrorString
ucl_parser_parse_string_pure s = unsafePerformIO $ do
    cs <- newCString s
    let p = ucl_parser_new 0x4
    didParse <- ucl_parser_add_string p cs (toEnum $ length s)
    if didParse 
    then return $ Left $ ucl_parser_get_object p
    else Right <$> peekCString ( ucl_parser_get_error p)

ucl_parser_add_file_pure :: String -> Either UCLObjectHandle ErrorString
ucl_parser_add_file_pure s = unsafePerformIO $ do
    cs <- newCString s
    let p = ucl_parser_new 0x4
    didParse <- ucl_parser_add_file p cs
    if didParse 
    then return $ Left $ ucl_parser_get_object p
    else Right <$> peekCString ( ucl_parser_get_error p)

unpack :: MSG.MessagePack a => String -> Either a ErrorString
unpack s = case ucl_parser_parse_string_pure s of
    (Right err) -> Right err
    (Left obj)  -> case MSG.fromObject (ucl_to_msgpack_object obj) of
        Nothing  -> Right "MessagePack fromObject Error" 
        (Just a) -> Left a

ucl_to_msgpack_object :: UCLObjectHandle -> MSG.Object
ucl_to_msgpack_object o = toMsgPackObj (ucl_object_type o) o
    where 
        toMsgPackObj n obj
            |n==ucl_OBJECT   = MSG.ObjectMap $ uclObjectToVector obj
            |n==ucl_ARRAY    = MSG.ObjectArray undefined
            |n==ucl_INT      = MSG.ObjectInt $ fromEnum $ ucl_object_toint obj
            |n==ucl_FLOAT    = MSG.ObjectDouble $ realToFrac $ ucl_object_todouble obj
            |n==ucl_STRING   = MSG.ObjectStr $ T.pack $ unsafePerformIO $ peekCString $ ucl_object_tostring obj
            |n==ucl_BOOLEAN  = MSG.ObjectBool $ ucl_object_toboolean obj
            |n==ucl_TIME     = error "time undefined"
            |n==ucl_USERDATA = error "userdata undefined"
            |n==ucl_NULL     = error "null undefined"
            |otherwise = error "\"Unknown Type\" Error"

uclObjectToVector :: UCLObjectHandle -> V.Vector (MSG.Object,MSG.Object)
uclObjectToVector o = iterateObject (ucl_object_iterate_safe iter True ) iter V.empty
    where 
        iter = ucl_object_iterate_new o
        iterateObject obj it vec = if ucl_object_type obj == ucl_NULL
            then vec
            else iterateObject (ucl_object_iterate_safe it True) it (V.snoc vec ( getUclKey obj , ucl_to_msgpack_object obj))
        getUclKey obj = MSG.ObjectStr $ T.pack $ unsafePerformIO $ peekCString $ ucl_object_key obj

uclArrayToVector :: UCLObjectHandle -> V.Vector MSG.Object
uclArrayToVector o = iterateArray (ucl_object_iterate_safe iter True ) iter V.empty
    where 
        iter = ucl_object_iterate_new o
        iterateArray obj it vec = if ucl_object_type obj == ucl_NULL
            then vec
            else iterateArray (ucl_object_iterate_safe it True) it (V.snoc vec (ucl_to_msgpack_object obj))


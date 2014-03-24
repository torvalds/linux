cmake_minimum_required (VERSION 2.6)

include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)

# set(skeinBlock_src skein_block.c)
set(skeinBlock_src skeinBlockNo3F.c)

set(skein_src 
    ${skeinBlock_src}
    skein.c
    skeinApi.c
    )

set(threefish_src
    threefishApi.c
    threefish256Block.c
    threefish512Block.c
    threefish1024Block.c
    )
set(s3f_src ${skein_src} ${threefish_src})

add_library(skein3fish SHARED ${s3f_src})
set_target_properties(skein3fish PROPERTIES VERSION ${VERSION} SOVERSION ${SOVERSION})
target_link_libraries(skein3fish ${LIBS})

install(TARGETS skein3fish DESTINATION ${LIBDIRNAME})

